#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

// Configurações comuns usadas na maioria dos exemplos do pico_w
// (veja https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html para detalhes)

/* ---------- Configurações Básicas do Sistema ---------- */
// Permite sobrescrita em alguns exemplos
#ifndef NO_SYS
#define NO_SYS 1
#endif
// Permite sobrescrita em alguns exemplos
#ifndef LWIP_SOCKET
#define LWIP_SOCKET 0
#endif

/* ---------- Configurações de Memória ---------- */
#if PICO_CYW43_ARCH_POLL
#define MEM_LIBC_MALLOC 1
#else
// MEM_LIBC_MALLOC é incompatível com versões não-polling
#define MEM_LIBC_MALLOC 0
#endif
#define MEM_ALIGNMENT 4
// Alterado de 16000 para (32 * 1024) conforme configuração otimizada
#define MEM_SIZE (32 * 1024)
// Alterado de 64 para 32 conforme configuração otimizada
#define MEMP_NUM_TCP_SEG 32
#define MEMP_NUM_ARP_QUEUE 10
#define PBUF_POOL_SIZE 32

/* ---------- Protocolos de Rede ---------- */
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_IPV4 1
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_DNS 1
#define LWIP_DHCP 1

/* ---------- Configurações TCP ---------- */
// Alterado de (16 * TCP_MSS) para (8 * TCP_MSS) para otimização
#define TCP_WND (8 * TCP_MSS)
#define TCP_MSS 1460
// Alterado de (16 * TCP_MSS) para (8 * TCP_MSS) para otimização
#define TCP_SND_BUF (8 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_TCP_KEEPALIVE 1

/* ---------- Configurações de Interface de Rede ---------- */
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_TX_SINGLE_PBUF 1
#define LWIP_NETCONN 0

/* ---------- Configurações DHCP ---------- */
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0

/* ---------- Estatísticas (Desabilitadas para Performance) ---------- */
#define MEM_STATS 0
#define SYS_STATS 0
#define MEMP_STATS 0
#define LINK_STATS 0

/* ---------- Outras Configurações ---------- */
// #define ETH_PAD_SIZE              2
#define LWIP_CHKSUM_ALGORITHM 3

/* ---------- Configurações de Debug ---------- */
#ifndef NDEBUG
#define LWIP_DEBUG 1
#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 1
#endif

// Todos os debugs desabilitados para otimização de performance
#define ETHARP_DEBUG LWIP_DBG_OFF
#define NETIF_DEBUG LWIP_DBG_OFF
#define PBUF_DEBUG LWIP_DBG_OFF
#define API_LIB_DEBUG LWIP_DBG_OFF
#define API_MSG_DEBUG LWIP_DBG_OFF
#define SOCKETS_DEBUG LWIP_DBG_OFF
#define ICMP_DEBUG LWIP_DBG_OFF
#define INET_DEBUG LWIP_DBG_OFF
#define IP_DEBUG LWIP_DBG_OFF
#define IP_REASS_DEBUG LWIP_DBG_OFF
#define RAW_DEBUG LWIP_DBG_OFF
#define MEM_DEBUG LWIP_DBG_OFF
#define MEMP_DEBUG LWIP_DBG_OFF
#define SYS_DEBUG LWIP_DBG_OFF
#define TCP_DEBUG LWIP_DBG_OFF
#define TCP_INPUT_DEBUG LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG LWIP_DBG_OFF
#define TCP_RTO_DEBUG LWIP_DBG_OFF
#define TCP_CWND_DEBUG LWIP_DBG_OFF
#define TCP_WND_DEBUG LWIP_DBG_OFF
#define TCP_FR_DEBUG LWIP_DBG_OFF
#define TCP_QLEN_DEBUG LWIP_DBG_OFF
#define TCP_RST_DEBUG LWIP_DBG_OFF
#define UDP_DEBUG LWIP_DBG_OFF
#define TCPIP_DEBUG LWIP_DBG_OFF
#define PPP_DEBUG LWIP_DBG_OFF
#define SLIP_DEBUG LWIP_DBG_OFF
#define DHCP_DEBUG LWIP_DBG_OFF

#endif /* __LWIPOPTS_H__ */
