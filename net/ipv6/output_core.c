/*
 * IPv6 library code, needed by static components when full IPv6 support is
 * not configured or static.  These functions are needed by GSO/GRO implementation.
 */
#include <linux/export.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/ip6_fib.h>
#include <net/addrconf.h>
#include <net/secure_seq.h>

/* This function exists only for tap drivers that must support broken
 * clients requesting UFO without specifying an IPv6 fragment ID.
 *
 * This is similar to ipv6_select_ident() but we use an independent hash
 * seed to limit information leakage.
 *
 * The network header must be set before calling this.
 */
void ipv6_proxy_select_ident(struct sk_buff *skb)
{
	static u32 ip6_proxy_idents_hashrnd __read_mostly;
	struct in6_addr buf[2];
	struct in6_addr *addrs;
	u32 hash, id;

	addrs = skb_header_pointer(skb,
				   skb_network_offset(skb) +
				   offsetof(struct ipv6hdr, saddr),
				   sizeof(buf), buf);
	if (!addrs)
		return;

	net_get_random_once(&ip6_proxy_idents_hashrnd,
			    sizeof(ip6_proxy_idents_hashrnd));

	hash = __ipv6_addr_jhash(&addrs[1], ip6_proxy_idents_hashrnd);
	hash = __ipv6_addr_jhash(&addrs[0], hash);

	id = ip_idents_reserve(hash, 1);
	skb_shinfo(skb)->ip6_frag_id = htonl(id);
}
EXPORT_SYMBOL_GPL(ipv6_proxy_select_ident);

int ip6_find_1stfragopt(struct sk_buff *skb, u8 **nexthdr)
{

	u16 offset = sizeof(struct ipv6hdr);
	unsigned int packet_len = skb_tail_pointer(skb) -
		skb_network_header(skb);
	int found_rhdr = 0;
	*nexthdr = &ipv6_hdr(skb)->nexthdr;

	while (offset <= packet_len) {
		struct ipv6_opt_hdr *exthdr;


		switch (**nexthdr) {

		case NEXTHDR_HOP:
			break;
		case NEXTHDR_ROUTING:
			found_rhdr = 1;
			break;
		case NEXTHDR_DEST:
#if IS_ENABLED(CONFIG_IPV6_MIP6)
			if (ipv6_find_tlv(skb, offset, IPV6_TLV_HAO) >= 0)
				break;
#endif
			if (found_rhdr)
				return offset;
			break;
		default:
			return offset;
		}

		if (offset + sizeof(struct ipv6_opt_hdr) > packet_len)
			return -EINVAL;

		exthdr = (struct ipv6_opt_hdr *)(skb_network_header(skb) +
						 offset);

		offset += ipv6_optlen(exthdr);

		*nexthdr = &exthdr->nexthdr;
	}

	return -EINVAL;
}
EXPORT_SYMBOL(ip6_find_1stfragopt);

#if IS_ENABLED(CONFIG_IPV6)
int ip6_dst_hoplimit(struct dst_entry *dst)
{
	int hoplimit = dst_metric_raw(dst, RTAX_HOPLIMIT);
	if (hoplimit == 0) {
		struct net_device *dev = dst->dev;
		struct inet6_dev *idev;

		rcu_read_lock();
		idev = __in6_dev_get(dev);
		if (idev)
			hoplimit = idev->cnf.hop_limit;
		else
			hoplimit = dev_net(dev)->ipv6.devconf_all->hop_limit;
		rcu_read_unlock();
	}
	return hoplimit;
}
EXPORT_SYMBOL(ip6_dst_hoplimit);
#endif

int __ip6_local_out(struct sk_buff *skb)
{
	int len;

	len = skb->len - sizeof(struct ipv6hdr);
	if (len > IPV6_MAXPLEN)
		len = 0;
	ipv6_hdr(skb)->payload_len = htons(len);
	IP6CB(skb)->nhoff = offsetof(struct ipv6hdr, nexthdr);

	return nf_hook(NFPROTO_IPV6, NF_INET_LOCAL_OUT, skb, NULL,
		       skb_dst(skb)->dev, dst_output);
}
EXPORT_SYMBOL_GPL(__ip6_local_out);

int ip6_local_out(struct sk_buff *skb)
{
	int err;

	err = __ip6_local_out(skb);
	if (likely(err == 1))
		err = dst_output(skb);

	return err;
}
EXPORT_SYMBOL_GPL(ip6_local_out);
