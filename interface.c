/* Code to manipulate interface information, shared between ifconfig and
   netstat. 

   10/1998 partly rewriten by Andi Kleen to support interface list.   
		   I don't claim that the list operations are efficient @).  
*/

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#if HAVE_AFIPX
#if (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 1)
#include <netipx/ipx.h>
#else
#include "ipx.h"
#endif
#endif

#if HAVE_AFECONET
#include <linux/if_ec.h>
#endif

#include "net-support.h"
#include "pathnames.h"
#include "version.h"
#include "proc.h"

#include "interface.h"
#include "sockets.h"
#include "util.h"
#include "intl.h"

int procnetdev_vsn = 1; 

static struct interface *int_list;

void add_interface(struct interface *n)
{
	struct interface *ife, **pp;

	pp = &int_list;
	for (ife = int_list; ife; pp = &ife->next, ife = ife->next) { 
		/* XXX: should use numerical sort */ 
		if (strcmp(ife->name, n->name) > 0) 
			break; 
	}
	n->next = (*pp);
	(*pp) = n;  
} 

struct interface *lookup_interface(char *name) 
{ 
	struct interface *ife;
	if (!int_list && (if_readlist()) < 0)
		return NULL;
	for (ife = int_list; ife; ife = ife->next) {  
		if (!strcmp(ife->name,name))
			break; 
	}
	return ife; 
}

int 
for_all_interfaces(int (*doit)(struct interface *, void *), void *cookie)
{   
	struct interface *ife;

	if (!int_list && (if_readlist() < 0))
		return -1;
	for (ife = int_list; ife; ife = ife->next) { 
		int err = doit(ife, cookie); 
		if (err) 
			return err;
	}
	return 0;
}

static int if_readconf(void)
{
	int numreqs = 30;
	struct ifconf ifc;
	struct ifreq *ifr; 
	int n, err = -1;  

	int sk; 
	sk = socket(PF_INET, SOCK_DGRAM, 0); 
	if (sk < 0) { 
		perror(_("error opening inet socket"));
		return -1; 
	}

	ifc.ifc_buf = NULL;
	for (;;) { 
		ifc.ifc_len = sizeof(struct ifreq) * numreqs; 
		ifc.ifc_buf = xrealloc(ifc.ifc_buf, ifc.ifc_len); 
		
		if (ioctl(sk, SIOCGIFCONF, &ifc) < 0) { 
			perror("SIOCGIFCONF");
			goto out; 
		}
		
		if (ifc.ifc_len == sizeof(struct ifreq)*numreqs) {
			/* assume it overflowed and try again */ 
			numreqs += 10; 
			continue;
		}
		break; 
	}

	for (ifr = ifc.ifc_req,n = 0; n < ifc.ifc_len; 
		 n += sizeof(struct ifreq), ifr++) {
		struct interface *ife;
		
		ife = lookup_interface(ifr->ifr_name);
		if (ife) 
			continue; 
		
		new(ife); 
		strcpy(ife->name, ifr->ifr_name);
		add_interface(ife); 
	}
	err = 0; 

out:
	free(ifc.ifc_buf);  
	close(sk);
	return err;  
}

static char *get_name(char *name, char *p)
{
	while (isspace(*p)) p++; 
	while (*p) { 
		if (isspace(*p)) 
			break; 
		if (*p == ':') { /* could be an alias */ 
			char *dot = p, *dotname = name; 
			*name++ = *p++; 
			while (isdigit(*p))
				*name++ = *p++; 
			if (*p != ':') { /* it wasn't, backup */ 
				p = dot; 
				name = dotname; 
			} 
			if (*p == '\0') return NULL; 
			p++;
			break; 
		}
		*name++ = *p++; 
	}
	*name++ = '\0';
	return p; 
} 

static int procnetdev_version(char *buf)
{
	if (strstr(buf,"compressed")) 
		return 3;
	if (strstr(buf,"bytes"))
		return 2;
	return 1; 
}

static int get_dev_fields(char *bp, struct interface *ife)
 {
	 switch (procnetdev_vsn) {
	 case 3:
		 sscanf(bp, 
			"%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",
					&ife->stats.rx_bytes,
					&ife->stats.rx_packets,
					&ife->stats.rx_errors,
					&ife->stats.rx_dropped,
					&ife->stats.rx_fifo_errors,
					&ife->stats.rx_frame_errors,
					&ife->stats.rx_compressed,
					&ife->stats.rx_multicast,
					
					&ife->stats.tx_bytes,
					&ife->stats.tx_packets,
					&ife->stats.tx_errors,
					&ife->stats.tx_dropped,
					&ife->stats.tx_fifo_errors,
					&ife->stats.collisions,
					&ife->stats.tx_carrier_errors,
					&ife->stats.tx_compressed);
		 break;
	 case 2:
		 sscanf(bp, "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",
					&ife->stats.rx_bytes,
					&ife->stats.rx_packets,
					&ife->stats.rx_errors,
					&ife->stats.rx_dropped,
					&ife->stats.rx_fifo_errors,
					&ife->stats.rx_frame_errors,
					
					&ife->stats.tx_bytes,
					&ife->stats.tx_packets,
					&ife->stats.tx_errors,
					&ife->stats.tx_dropped,
					&ife->stats.tx_fifo_errors,
					&ife->stats.collisions,
					&ife->stats.tx_carrier_errors);
		 ife->stats.rx_multicast = 0;
		 break;
	 case 1:
		   sscanf(bp, "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",
				  &ife->stats.rx_packets,
				  &ife->stats.rx_errors,
				  &ife->stats.rx_dropped,
				  &ife->stats.rx_fifo_errors,
				  &ife->stats.rx_frame_errors,
				  
				  &ife->stats.tx_packets,
				  &ife->stats.tx_errors,
				  &ife->stats.tx_dropped,
				  &ife->stats.tx_fifo_errors,
				  &ife->stats.collisions,
				  &ife->stats.tx_carrier_errors);
		   ife->stats.rx_bytes = 0;
		   ife->stats.tx_bytes = 0;
		   ife->stats.rx_multicast = 0;
		   break;
	 }
	 return 0;
}

int if_readlist(void) 
{
	FILE *fh;
	char buf[512];
	struct interface *ife; 
	int err;
	
	fh = fopen(_PATH_PROCNET_DEV,"r"); 
	if (!fh) { 
		perror(_PATH_PROCNET_DEV); 
		return -1;
	} 

	fgets(buf,sizeof buf,fh);  /* eat line */ 
	fgets(buf,sizeof buf,fh); 

#if 0 /* pretty, but can't cope with missing fields */   
	fmt = proc_gen_fmt(_PATH_PROCNET_DEV, 1, fh,
					   "face", "", /* parsed separately */
					   "bytes",  "%lu",  
					   "packets", "%lu",
					   "errs", "%lu",
					   "drop", "%lu",
					   "fifo", "%lu", 
					   "frame", "%lu", 
					   "compressed", "%lu",
					   "multicast", "%lu", 
					   "bytes", "%lu",
					   "packets", "%lu",
					   "errs", "%lu",
					   "drop", "%lu",
					   "fifo", "%lu",
					   "colls", "%lu",
					   "carrier", "%lu",
					   "compressed", "%lu",  
					   NULL); 
	if (!fmt) 
		return -1; 
#else
	procnetdev_vsn = procnetdev_version(buf); 
#endif	

	err = 0; 	
	while (fgets(buf,sizeof buf,fh)) { 
		char *s; 

		new(ife); 
	
		s = get_name(ife->name, buf);    
		get_dev_fields(s, ife);
		ife->statistics_valid = 1;

		add_interface(ife);
	}
	if (ferror(fh)) {
		perror(_PATH_PROCNET_DEV); 
		err = -1; 
	} 
	
	if (!err) 
		err = if_readconf();  

#if 0
	free(fmt); 
#endif
	return err; 
} 

/* Support for fetching an IPX address */

#if HAVE_AFIPX
static int ipx_getaddr(int sock, int ft, struct ifreq *ifr)
{
  ((struct sockaddr_ipx *)&ifr->ifr_addr)->sipx_type=ft;
  return ioctl(sock, SIOCGIFADDR, ifr);
}
#endif

/* Fetch the interface configuration from the kernel. */
int
if_fetch(char *ifname, struct interface *ife)
{
  struct ifreq ifr;

  strcpy(ifr.ifr_name, ifname);
  if (ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0) return(-1);
  ife->flags = ifr.ifr_flags;

  strcpy(ifr.ifr_name, ifname);
  if (ioctl(skfd, SIOCGIFHWADDR, &ifr) < 0) 
    memset(ife->hwaddr, 0, 32);
  else 
    memcpy(ife->hwaddr,ifr.ifr_hwaddr.sa_data,8);

  ife->type=ifr.ifr_hwaddr.sa_family;

  strcpy(ifr.ifr_name, ifname);
  if (ioctl(skfd, SIOCGIFMETRIC, &ifr) < 0)
    ife->metric = 0;
  else 
    ife->metric = ifr.ifr_metric;

  strcpy(ifr.ifr_name, ifname);
  if (ioctl(skfd, SIOCGIFMTU, &ifr) < 0)
    ife->mtu = 0;
  else 
    ife->mtu = ifr.ifr_mtu;

  strcpy(ifr.ifr_name, ifname);
  if (ioctl(skfd, SIOCGIFMAP, &ifr) < 0)
    memset(&ife->map, 0, sizeof(struct ifmap));
  else 
    memcpy(&ife->map,&ifr.ifr_map,sizeof(struct ifmap));

  strcpy(ifr.ifr_name, ifname);
  if (ioctl(skfd, SIOCGIFMAP, &ifr) < 0)
    memset(&ife->map, 0, sizeof(struct ifmap));
  else 
    ife->map = ifr.ifr_map;

#ifdef HAVE_TXQUEUELEN
  strcpy(ifr.ifr_name, ifname);
  if (ioctl(skfd, SIOCGIFTXQLEN, &ifr) < 0)
    ife->tx_queue_len = -1; /* unknown value */
  else 
    ife->tx_queue_len = ifr.ifr_qlen;
#else
  ife->tx_queue_len = -1; /* unknown value */
#endif

#if HAVE_AFINET
  strcpy(ifr.ifr_name, ifname);
  if (inet_sock < 0 || ioctl(inet_sock, SIOCGIFDSTADDR, &ifr) < 0)
    memset(&ife->dstaddr, 0, sizeof(struct sockaddr));
  else 
    ife->dstaddr = ifr.ifr_dstaddr;

  strcpy(ifr.ifr_name, ifname);
  if (inet_sock < 0 || ioctl(inet_sock, SIOCGIFBRDADDR, &ifr) < 0)
    memset(&ife->broadaddr, 0, sizeof(struct sockaddr));
  else 
    ife->broadaddr = ifr.ifr_broadaddr;

  strcpy(ifr.ifr_name, ifname);
  if (inet_sock < 0 || ioctl(inet_sock, SIOCGIFNETMASK, &ifr) < 0)
    memset(&ife->netmask, 0, sizeof(struct sockaddr));
  else 
    ife->netmask = ifr.ifr_netmask;

  strcpy(ifr.ifr_name, ifname);
  if (inet_sock < 0 || ioctl(inet_sock, SIOCGIFADDR, &ifr) < 0) 
    memset(&ife->addr, 0, sizeof(struct sockaddr));
  else 
    ife->addr = ifr.ifr_addr;
#endif
  
#if HAVE_AFATALK
  /* DDP address maybe ? */
  strcpy(ifr.ifr_name, ifname);
  if (ddp_sock >= 0 && ioctl(ddp_sock, SIOCGIFADDR, &ifr) == 0) {
    ife->ddpaddr=ifr.ifr_addr;
    ife->has_ddp=1;
  }
#endif

#if HAVE_AFIPX  
  /* Look for IPX addresses with all framing types */
  strcpy(ifr.ifr_name, ifname);
  if (ipx_sock >= 0) {
    if (!ipx_getaddr(ipx_sock, IPX_FRAME_ETHERII, &ifr)) {
      ife->has_ipx_bb=1;
      ife->ipxaddr_bb=ifr.ifr_addr;
    }
    strcpy(ifr.ifr_name, ifname);
    if (!ipx_getaddr(ipx_sock, IPX_FRAME_SNAP, &ifr)) {
      ife->has_ipx_sn=1;
      ife->ipxaddr_sn=ifr.ifr_addr;
    }
    strcpy(ifr.ifr_name, ifname);
    if(!ipx_getaddr(ipx_sock, IPX_FRAME_8023, &ifr)) {
      ife->has_ipx_e3=1;
      ife->ipxaddr_e3=ifr.ifr_addr;
    }
    strcpy(ifr.ifr_name, ifname);
    if(!ipx_getaddr(ipx_sock, IPX_FRAME_8022, &ifr)) {
      ife->has_ipx_e2=1;
      ife->ipxaddr_e2=ifr.ifr_addr;
    }
  }
#endif

#if HAVE_AFECONET
  /* Econet address maybe? */
  strcpy(ifr.ifr_name, ifname);
  if (ec_sock >= 0 && ioctl(ec_sock, SIOCGIFADDR, &ifr) == 0) {
    ife->ecaddr = ifr.ifr_addr;
    ife->has_econet = 1;
  }
#endif

  return 0;
}
