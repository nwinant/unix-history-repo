/*
 * Copyright (c) 1990 Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 *
 *	@(#)if_dp.c	7.3 (Berkeley) %G%
 */

#include "dp.h"
#if NDP > 0

/*
 * DPV-11 device driver, X.25 version
 *
 * Derived from dmc-11 driver:
 *
 *	Bill Nesheim
 *	Cornell University
 *
 *	Lou Salkind
 *	New York University
 */

/* #define DEBUG	/* for base table dump on fatal error */

#include "machine/pte.h"

#include "param.h"
#include "systm.h"
#include "mbuf.h"
#include "buf.h"
#include "ioctl.h"		/* must precede tty.h */
#include "protosw.h"
#include "socket.h"
#include "socketvar.h"
#include "syslog.h"
#include "vmmac.h"
#include "errno.h"
#include "time.h"
#include "kernel.h"

#include "../net/if.h"
#include "../net/netisr.h"
#include "../net/route.h"

#include "../vax/cpu.h"
#include "../vax/mtpr.h"

#define	dzdevice dpdevice
#include "../vaxuba/pdma.h"
#include "../vaxuba/ubavar.h"

#include "../netccitt/x25.h"
#include "../netccitt/pk.h"
#include "../netccitt/pk_var.h"

#include "if_dpreg.h"

/*
 * Driver information for auto-configuration stuff.
 */
int	dpprobe(), dpattach(), dpinit(), dpioctl(), dprint(), dpxint();
int	dpoutput(), dpreset(), dptimeout(), dpstart(), x25_ifoutput(), dptestoutput();

struct	uba_device *dpinfo[NDP];

u_short	dpstd[] = { 0 };
struct	uba_driver dpdriver =
	{ dpprobe, 0, dpattach, 0, dpstd, "dp", dpinfo };

/*
 * debug info
 */
struct	dpstat {
	long	start;
	long	nohdr;
	long	init;
	long	rint;
	long	xint;
	long	reset;
	long	ioctl;
	long	down;
	long	mchange;
	long	timeout;
} dpstat;
/*
 * Pdma structures for fast interrupts.
 */
struct	pdma dppdma[2*NDP];

/* error reporting intervals */
#define DPI_RPNBFS	50
#define DPI_RPDSC	1
#define DPI_RPTMO	10
#define DPI_RPDCK	10


/*
 * DP software status per interface.
 *
 * Each interface is referenced by a network interface structure,
 * dp_if, which the routing code uses to locate the interface.
 * This structure contains the output queue for the interface, its address, ...
 */
struct dp_softc {
	struct	ifnet dp_if;		/* network-visible interface */
	int	dp_ipl;
	short	dp_iused;		/* input buffers given to DP */
	short	dp_flags;		/* flags */
#define DPF_RUNNING	0x01		/* device initialized */
#define DPF_ONLINE	0x02		/* device running (had a RDYO) */
#define DPF_RESTART	0x04		/* software restart in progress */
#define DPF_FLUSH	0x08		/* had a ROVR, flush ipkt when done */
	short	dp_ostate;		/* restarting, etc. */
	short	dp_istate;		/* not sure this is necessary */
#define DPS_IDLE	0
#define DPS_RESTART	1
#define DPS_ACTIVE	2
#define DPS_XEM		3		/* transmitting CRC, etc. */
	int	dp_errors[4];		/* non-fatal error counters */
#define dp_datck dp_errors[0]
#define dp_timeo dp_errors[1]
#define dp_nobuf dp_errors[2]
#define dp_disc  dp_errors[3]
	struct timeval	dp_xt;			/* start of last transmission */
	char	dp_obuf[DP_MTU+8];
	char	dp_ibuf[DP_MTU+8];
} dp_softc[NDP];

dpprobe(reg, ui)
	caddr_t reg;
	struct	uba_device *ui;
{
	register int br, cvec;
	register struct dpdevice *addr = (struct dpdevice *)reg;
	register int i;

#ifdef lint
	br = 0; cvec = br; br = cvec;
	dprint(0); dpxint(0);
#endif
	(void) spl6();
	addr->dpclr = DP_CLR;
	addr->dpclr = DP_XIE|DP_XE;
	DELAY(100000);
	dp_softc[ui->ui_unit].dp_ipl = br = qbgetpri();
	addr->dpclr = 0;
	if (cvec && cvec != 0x200){
		cvec -= 4;
	}
	return (1);
}

/*
 * Interface exists: make available by filling in network interface
 * record.  System will initialize the interface when it is ready
 * to accept packets.
 */
dpattach(ui)
	register struct uba_device *ui;
{
	register struct dp_softc *dp = &dp_softc[ui->ui_unit];

	dp->dp_if.if_unit = ui->ui_unit;
	dp->dp_if.if_name = "dp";
	dp->dp_if.if_mtu = DP_MTU;
	dp->dp_if.if_init = dpinit;
	dp->dp_if.if_output = x25_ifoutput;
	dp->dp_if.if_start = dpstart;
	dp->dp_if.if_ioctl = dpioctl;
	dp->dp_if.if_reset = dpreset;
	dp->dp_if.if_watchdog = dptimeout;
	dp->dp_if.if_flags = 0;
	if_attach(&dp->dp_if);
}

/*
 * Reset of interface after UNIBUS reset.
 * If interface is on specified UBA, reset its state.
 */
dpreset(unit, uban)
	int unit, uban;
{
	register struct uba_device *ui;
	register struct dp_softc *dp = &dp_softc[unit];
	register struct dpdevice *addr;

	dpstat.reset++;
	if (unit >= NDP || (ui = dpinfo[unit]) == 0 || ui->ui_alive == 0 ||
	    ui->ui_ubanum != uban)
		return;
	printf(" dp%d", unit);
	dp->dp_flags = 0;
	dp->dp_if.if_flags &= ~IFF_RUNNING;
	addr = (struct dpdevice *)ui->ui_addr;
	addr->dpclr = DP_CLR;
	DELAY(1000);
	addr->dpsar = 0;
	addr->dprcsr = 0;
	dpinit(unit);
}

/*
 * Initialization of interface.
 */
dpinit(unit)
	int unit;
{
	register struct dp_softc *dp = &dp_softc[unit];
	register struct uba_device *ui = dpinfo[unit];
	register struct dpdevice *addr;
	register struct ifaddr *ifa;
	register struct pdma *pdp = &dppdma[unit*2];
	int base, s;

	dpstat.init++;
	/*
	 * Check to see that an address has been set.
	 */
	for (ifa = dp->dp_if.if_addrlist; ifa; ifa = ifa->ifa_next)
		if (ifa->ifa_addr->sa_family != AF_LINK)
			break;
	if (ifa == (struct ifaddr *) 0)
		return;

	addr = (struct dpdevice *)ui->ui_addr;
	s = splimp();
	dp->dp_iused = 0;
	dp->dp_istate = dp->dp_ostate = DPS_IDLE;
	dp->dp_if.if_flags |= IFF_RUNNING;
	dp->dp_if.if_flags &= ~IFF_OACTIVE;

	pdp->p_addr = addr;
	pdp->p_fcn = dpxint;
	pdp->p_mem = pdp->p_end = dp->dp_obuf;
	pdp++;
	pdp->p_addr = addr;
	pdp->p_fcn = dprint;
	/* force initial interrupt to come to dprint */
	pdp->p_mem = pdp->p_end = dp->dp_ibuf + DP_MTU + 8;

	addr->dpclr = DP_CLR;
	DELAY(1000);
	addr->dpclr = 0;
	DELAY(1000);
	/* DP_ATA = 0, DP_CHRM = 0,
			    CRC = CCIIT, initially all ones, 2nd addr = 0 */
	addr->dpsar = 0;
	/* enable receive interrupt */
	addr->dprcsr = DP_RIE | DP_MIE | DP_RE | DP_DTR | DP_RTS;
	dpstart(&dp->dp_if);
	splx(s);
}

int dpMessy = 0;
/*
 * Start output on interface.  Get another datagram
 * to send from the interface queue and map it to
 * the interface before starting output.
 *
 */
dpstart(ifp)
	struct ifnet *ifp;
{
	int s, unit = ifp->if_unit, error = 0, len;
	register struct dp_softc *dp = &dp_softc[unit];
	register struct dpdevice *addr =
				(struct dpdevice *)(dpinfo[unit]->ui_addr);
	register struct mbuf *m;
	register char *cp;
	char *cplim;

	/*
	 * If already doing output, go away and depend on transmit
	 * complete or error.
	 */
	dpstat.start++;
	if (dp->dp_if.if_flags & IFF_OACTIVE)
		goto out;
	IF_DEQUEUE(&dp->dp_if.if_snd, m);
	if (m == 0)
		goto out;
	dp->dp_if.if_collisions++;
	if (m->m_flags & M_PKTHDR)
		len = m->m_pkthdr.len;
	else {
		struct mbuf *m0 = m;
		for (len = 0; m; m = m->m_next)
			len += m->m_len;
		m = m0;
		dpstat.nohdr++;
	}
	if (len == 0)
		goto out;
	if (len > DP_MTU) {
		error = EINVAL;
		goto out;
	}
	cp = dp->dp_obuf;
	dppdma[2*unit].p_mem = cp + 1;
	while (m) {
		struct mbuf *n;
		bcopy(mtod(m, caddr_t), (caddr_t)cp, m->m_len);
		cp += m->m_len;
		MFREE(m, n); m = n;
	}
	dppdma[2*unit].p_end = cp;
	if (dpMessy)
		printf("dp: len %d, start %x, end %x, first %x, next %x",
			len, dp->dp_obuf, cp, *(int *)dp->dp_obuf,
			1[(int *)dp->dp_obuf]);
	dp->dp_if.if_flags |= IFF_OACTIVE;
	dp->dp_ostate = DPS_ACTIVE;
	dp->dp_if.if_collisions--;
dp->dp_xt = time;
	addr->dpclr = DP_XE | DP_XIE;
	addr->dptdsr = DP_XSM | (0xff & dp->dp_obuf[0]);
out:
	return (error);
}
/*
 * Receive done or error interrupt
 */
dprint(unit, pdma, addr)
register struct pdma *pdma;
register struct dpdevice *addr;
{
	register struct dp_softc *dp = &dp_softc[unit];
	short rdsr = addr->dprdsr, rcsr;

	dpstat.rint++;
	splx(dp->dp_ipl);
	if (rdsr & DP_RSM) { /* Received Start of Message */
		dp->dp_ibuf[0] = rdsr & DP_RBUF;
		pdma->p_mem = dp->dp_ibuf + 1;
		dp->dp_flags &= ~DPF_FLUSH;
		return;
	}
	if (rdsr & DP_REM) { /* Received End of Message */
		if (rdsr & DP_REC || dp->dp_flags & DPF_FLUSH) {
			dp->dp_if.if_ierrors++;
		} else
			dpinput(&dp->dp_if,
				pdma->p_mem - dp->dp_ibuf, dp->dp_ibuf);
		pdma->p_mem = pdma->p_end;
		dp->dp_flags &= ~ DPF_FLUSH;
		return;
	}
	if (rdsr & DP_ROVR) {
		dp->dp_flags |= DPF_FLUSH;
		return;
	}
	rcsr = pdma->p_arg;
	if (rcsr & DP_MSC) {
		dpstat.mchange++;
		if (0 == (rcsr & DP_DSR)) {
			log(LOG_DEBUG, "dp%d: lost modem\n", unit);
			dpdown(unit);
		}
		return;
	}
	dp->dp_flags |= DPF_FLUSH;
	if (pdma->p_mem != pdma->p_end)
		log("dp%d: unexplained receiver interrupt\n", unit);
}
struct timeval dp_xt;

/*
 * Transmit complete or error interrupt
 */
dpxint(unit, pdma, addr)
register struct pdma *pdma;
register struct dpdevice *addr;
{
	register struct dp_softc *dp = &dp_softc[unit];
	int s;

	splx(dp->dp_ipl);
	dpstat.xint++;
	if (addr->dptdsr & DP_XERR) {
		log("if_dp%d: data late\n", unit);
	restart:
		pdma->p_mem = dp->dp_obuf;
		addr->dptdsr = DP_XSM;
		dp->dp_if.if_oerrors++;
		return;
	}
	switch (dp->dp_ostate) {

	case DPS_ACTIVE:
		if (pdma->p_mem != pdma->p_end) {
			log("if_dp%d: misc error in dpxint\n", unit);
			goto restart;
		}
		addr->dptdsr = DP_XEM;
		dp->dp_ostate = DPS_XEM;
		break;

	case DPS_XEM:
		dp->dp_if.if_opackets++;
		dp->dp_if.if_flags &= ~IFF_OACTIVE;
s = splclock();
dp_xt = time;
timevalsub(&dp_xt, &dp->dp_xt);
splx(s);
		if (dp->dp_if.if_snd.ifq_len)
			dpstart(&dp->dp_if);
		else {
			addr->dpclr = 0;
			dp->dp_ostate = DPS_IDLE;
		}
		break;

	default:
		log("if_dp%d: impossible state in dpxint\n");
	}
}
/*
 * Routine to copy from device buffer into mbufs.
 *
 * Warning: This makes the fairly safe assumption that
 * mbufs have even lengths.
 */
struct mbuf *
dpget(rxbuf, totlen, off, ifp)
	caddr_t rxbuf;
	int totlen, off;
	struct ifnet *ifp;
{
	register caddr_t cp;
	register struct mbuf *m;
	struct mbuf *top = 0, **mp = &top;
	int len;
	caddr_t packet_end;

	cp = rxbuf;
	packet_end = cp + totlen;
	if (off) {
		off += 2 * sizeof(u_short);
		totlen -= 2 *sizeof(u_short);
		cp = rxbuf + off;
	}

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == 0)
		return (0);
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = totlen;
	m->m_len = MHLEN;

	while (totlen > 0) {
		if (top) {
			MGET(m, M_DONTWAIT, MT_DATA);
			if (m == 0) {
				m_freem(top);
				return (0);
			}
			m->m_len = MLEN;
		}
		len = min(totlen, (packet_end - cp));
		if (len >= MINCLSIZE) {
			MCLGET(m, M_DONTWAIT);
			if (m->m_flags & M_EXT)
				m->m_len = len = min(len, MCLBYTES);
			else
				len = m->m_len;
		} else {
			/*
			 * Place initial small packet/header at end of mbuf.
			 */
			if (len < m->m_len) {
				if (top == 0 && len + max_linkhdr <= m->m_len)
					m->m_data += max_linkhdr;
				m->m_len = len;
			} else
				len = m->m_len;
		}
		bcopy(cp, mtod(m, caddr_t), (u_int)len);
		*mp = m;
		mp = &m->m_next;
		totlen -= len;
		cp += len;
		if (cp == packet_end)
			cp = rxbuf;
	}
	return (top);
}

dpinput(ifp, len, buffer)
register struct ifnet *ifp;
caddr_t buffer;
{
	register struct ifqueue *inq;
	register struct mbuf *m;
	extern struct ifqueue hdintrq, ipintrq;
	int netisr;

	ifp->if_ipackets++;
    {
	register struct ifaddr *ifa = ifp->if_addrlist;
	register u_char *cp = (u_char *)buffer;

	for (ifa = ifp->if_addrlist; ifa; ifa = ifa->ifa_next)
		if (ifa->ifa_addr->sa_family != AF_LINK)
			break;
	if (cp[0] == 0xff && cp[1] == 0x3) {
		/* This is a UI HDLC Packet, so we'll assume PPP
		   protocol.  for now, IP only. */
		buffer += 4;
		len -= 4;
		inq = &ipintrq;
		netisr = NETISR_IP;
	} else {
		inq = &hdintrq;
		netisr = NETISR_CCITT;
	}
    }
	if (len <= 0)
		return;

	m = dpget(buffer, len , 0, ifp);
	if (m == 0)
		return;

	if(IF_QFULL(inq)) {
		IF_DROP(inq);
		m_freem(m);
	} else {
		IF_ENQUEUE(inq, m);
		schednetisr(netisr);
	}
}

/*
 * Process an ioctl request.
 */
dpioctl(ifp, cmd, data)
	register struct ifnet *ifp;
	int cmd;
	caddr_t data;
{
	register struct ifaddr *ifa = (struct ifaddr *)data;
	int s = splimp(), error = 0;
	struct dp_softc *dp = &dp_softc[ifp->if_unit];

	dpstat.ioctl++;
	switch (cmd) {

	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		switch (ifa->ifa_addr->sa_family) {
		case AF_INET:
			ifp->if_output = dptestoutput;
		default:
			dpinit(ifp->if_unit);
			break;
		}
		break;
#ifdef CCITT
	case SIOCSIFCONF_X25:
		ifp->if_flags |= IFF_UP;
		ifp->if_output = x25_ifoutput;
		error = hd_ctlinput(PRC_IFUP, ifa->ifa_addr);
		if (error == 0)
			dpinit(ifp->if_unit);
		break;
#endif

	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP) == 0 &&
		    (ifp->if_flags & IFF_RUNNING))
			dpdown(ifp->if_unit);
		else if (ifp->if_flags & IFF_UP &&
		    (ifp->if_flags & IFF_RUNNING) == 0)
			dpinit(ifp->if_unit);
		break;


	default:
		error = EINVAL;
	}
	splx(s);
	return (error);
}
/*
 * Reset a device and mark down.
 * Flush output queue and drop queue limit.
 */
dpdown(unit)
	int unit;
{
	register struct dp_softc *dp = &dp_softc[unit];

	dpstat.down++;
	if_qflush(&dp->dp_if.if_snd);
	dpreset(unit);
}

/*
 * Watchdog timeout to see that transmitted packets don't
 * lose interrupts.  The device has to be online (the first
 * transmission may block until the other side comes up).
 */
dptimeout(unit)
	int unit;
{
	register struct dp_softc *dp;

	/* currently not armed */
	dpstat.timeout++;
	dp = &dp_softc[unit];
	if (dp->dp_if.if_flags & IFF_OACTIVE) {
		dpstart(&dp->dp_if);
	}
}
/*
 * For debugging loopback activity.
 */
static char pppheader[4] = { -1, 3, 0, 0x21 };
int dp_louts;

dptestoutput(ifp, m, dst, rt)
register struct ifnet *ifp;
register struct mbuf *m;
struct sockaddr *dst;
struct rtentry *rt;
{
	/*
	 * Queue message on interface, and start output if interface
	 * not yet active.
	 */
	int s = splimp(), error = 0;
	dp_louts++;
	M_PREPEND(m, sizeof pppheader, M_DONTWAIT);
	if (m == 0) {
		splx(s);
		return ENOBUFS;
	}
	bcopy(pppheader, mtod(m, caddr_t), sizeof pppheader);
	if (IF_QFULL(&ifp->if_snd)) {
		IF_DROP(&ifp->if_snd);
	    /* printf("%s%d: HDLC says OK to send but queue full, may hang\n",
			ifp->if_name, ifp->if_unit);*/
		m_freem(m);
		error = ENOBUFS;
	} else {
		IF_ENQUEUE(&ifp->if_snd, m);
		if ((ifp->if_flags & IFF_OACTIVE) == 0)
			(*ifp->if_start)(ifp);
	}
	splx(s);
	return (error);
}

#endif
