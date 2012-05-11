/* rawif.c
 * See lwIP source for license 
 */
#include "lwip/netif.h"

#include <tables.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include "lwip/debug.h"
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/ip.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/dhcp.h"
#include "lwip/sys.h"

#include "netif/etharp.h"
#include <net/if.h>
#include <linux/if_ether.h>
#include <sys/ioctl.h>
#include <netpacket/packet.h>

struct rawif {
  int fd;
  struct sockaddr_ll dev;
} rawif_state;
struct netif raw_netif;
static struct dhcp dhcp_state;
extern uint8_t mac_address[6];

/*-----------------------------------------------------------------------------------*/
/*
 * low_level_output():
 *
 * Should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 */
/*-----------------------------------------------------------------------------------*/

static err_t
low_level_output(struct netif *netif, struct pbuf *p)
{
  struct pbuf *q;
  char buf[1514];
  char *bufptr;
  struct rawif *rawif;

  rawif = (struct rawif *)netif->state;
#if 0
    if(((double)rand()/(double)RAND_MAX) < 0.2) {
    printf("drop output\n");
    return ERR_OK;
    }
#endif

  bufptr = buf;
  for (q = p; q; q = q->next) {
    memcpy(bufptr, q->payload, q->len);
    bufptr += q->len;
  }

  if (sendto(rawif->fd, buf, p->tot_len, 0, (void*)&rawif->dev, sizeof(rawif->dev)) < 0) {
    perror("rawif: send");
  }
  return ERR_OK;
}
/*-----------------------------------------------------------------------------------*/
/*
 * low_level_input():
 *
 * Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 *
 */
/*-----------------------------------------------------------------------------------*/
static struct pbuf *
low_level_input(struct rawif *rawif)
{
  struct pbuf *p, *q;
  u16_t len;
  char buf[1514];
  char *bufptr;

  /* Obtain the size of the packet and put it into the "len"
     variable. */
  len = read(rawif->fd, buf, sizeof(buf));
#if 0
    if(((double)rand()/(double)RAND_MAX) < 0.2) {
    printf("drop\n");
    return NULL;
    }
#endif

  /* We allocate a pbuf chain of pbufs from the pool. */
  p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);

  if(p != NULL) {
    /* We iterate over the pbuf chain until we have read the entire
       packet into the pbuf. */
    bufptr = &buf[0];
    for(q = p; q != NULL; q = q->next) {
      /* Read enough bytes to fill this pbuf in the chain. The
         available data in the pbuf is given by the q->len
         variable. */
      /* read data into(q->payload, q->len); */
      memcpy(q->payload, bufptr, q->len);
      bufptr += q->len;
    }
    /* acknowledge that packet has been read(); */
  } else {
    /* drop packet(); */
  }

  return p;
}

/*-----------------------------------------------------------------------------------*/
void
rawif_poll(void) {
  struct rawif *rawif;
  fd_set fdset;
  int ret;

  rawif = (struct rawif *)raw_netif.state;

  FD_ZERO(&fdset);
  FD_SET(rawif->fd, &fdset);

  /* Wait up to 1ms for a packet to arrive. */
  struct timeval t = {
    .tv_sec = 0,
    .tv_usec = 1000
  };
  ret = select(rawif->fd + 1, &fdset, NULL, NULL, &t);

  if (ret < 0) {
    perror("rawif_poll: select");
    return;
  }

  if (!ret) return;

  struct pbuf *p = low_level_input(rawif);
  if (!p) return;
  struct eth_hdr *ethhdr = (struct eth_hdr *)p->payload;

  switch(htons(ethhdr->type)) {
  /* IP or ARP packet? */
  case ETHTYPE_IP:
  case ETHTYPE_ARP:
#if PPPOE_SUPPORT
  /* PPPoE packet? */
  case ETHTYPE_PPPOEDISC:
  case ETHTYPE_PPPOE:
#endif /* PPPOE_SUPPORT */
    /* full packet send to tcpip_thread to process */
    raw_netif.input(p, &raw_netif);
    break;
  default:
    pbuf_free(p);
    break;
  }
}

void rawif_init(void) {

	printf("rawif_init\n");

	struct ifreq ifr;
	int sock;

	if ((sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
		perror("socket");
		exit(1);
	}

	/* Set the device to use */
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, "eth0");

	if (ioctl(sock, SIOCGIFFLAGS, &ifr) == -1) {
		perror("ioctl(SIOCGIFFLAGS)");
		exit(1);
	}

	ifr.ifr_flags |= IFF_PROMISC;
	if (ioctl(sock, SIOCSIFFLAGS, &ifr) == -1) {
		perror("ioctl(SIOCSIFFLAGS)");
		exit(1);
	}

	printf("Set promiscuous mode\n");

	if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
		perror("ioctl(SIOCGIFINDEX)");
		exit(1);
	}

	memset(&rawif_state.dev, 0, sizeof(rawif_state.dev));
	rawif_state.dev.sll_family = AF_PACKET;
	rawif_state.dev.sll_halen = htons(6);
	memcpy(&rawif_state.dev.sll_addr, mac_address, 6);
	rawif_state.dev.sll_ifindex = ifr.ifr_ifindex;

	rawif_state.fd = sock;

	raw_netif.output = etharp_output_FPV_netif_output;
	raw_netif.linkoutput = low_level_output;
	raw_netif.name[0] = 'e';
	raw_netif.name[1] = 'n';
	raw_netif.hwaddr_len = ETHARP_HWADDR_LEN;
	raw_netif.flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
	raw_netif.mtu = 1500;
	memcpy(raw_netif.hwaddr, mac_address, 6);

	struct ip_addr ipa = { 0 } , netmask = { 0 } , gw = { 0 };

	raw_netif.state = &rawif_state;
	netif_add(&raw_netif, &ipa, &netmask, &gw, &rawif_state, ethernet_input);
	netif_set_default(&raw_netif);
	netif_set_up(&raw_netif);
	dhcp_start(&raw_netif, &dhcp_state);
}

INITIALIZER(hardware, rawif_init);
INITIALIZER(poll, rawif_poll);
