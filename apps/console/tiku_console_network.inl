/* The same rootless ICMP-over-SLIP workflow as tikuConsole/tcon/packets.py.
 * This does not create a TUN interface, route, firewall rule or NAT bridge. */
static unsigned checksum(const unsigned char *p, size_t n)
{
    unsigned sum = 0;
    while (n > 1) { sum += ((unsigned)p[0] << 8) | p[1]; p += 2; n -= 2; }
    if (n) sum += (unsigned)p[0] << 8;
    while (sum >> 16) sum = (sum & 65535u) + (sum >> 16);
    return (~sum) & 65535u;
}
static void put16(unsigned char *p, unsigned n)
{ p[0] = (unsigned char)(n >> 8); p[1] = (unsigned char)n; }
static unsigned get16(const unsigned char *p)
{ return ((unsigned)p[0] << 8) | p[1]; }

static size_t slip_encode(const unsigned char *p, size_t n, unsigned char *out, size_t cap)
{
    size_t used = 0;
    if (cap < 2) return 0;
    out[used++] = 0xc0;
    for (size_t i = 0; i < n; i++) {
        size_t need = p[i] == 0xc0 || p[i] == 0xdb ? 2 : 1;
        if (used + need + 1 > cap) return 0;
        if (need == 2) { out[used++] = 0xdb; out[used++] = p[i] == 0xc0 ? 0xdc : 0xdd; }
        else out[used++] = p[i];
    }
    out[used++] = 0xc0; return used;
}

static void ping_summary(console_t *st)
{
    int lost = st->ping_sent ? 100 * (st->ping_sent - st->ping_received) / st->ping_sent : 0;
    snprintf(st->net_note, sizeof st->net_note,
        "%s | %d sent, %d replies, %d%% %s | min/avg/max %.1f / %.1f / %.1f ms",
        st->ping_active ? "Pinging" : "Finished", st->ping_sent, st->ping_received, lost,
        st->ping_active ? "unanswered" : "loss", st->ping_received ? st->rtt_min : 0,
        st->ping_received ? st->rtt_sum / st->ping_received : 0, st->rtt_max);
    st->dirty = 1;
}

/** Validate lengths, fragmentation, addresses, checksums and duplicate replies. */
static void ip_packet(console_t *st, const unsigned char *p, size_t n)
{
    if (!st->ping_active || n < 28 || p[0] >> 4 != 4 || p[9] != 1) return;
    size_t ihl = (p[0] & 15u) * 4u, total = get16(p + 2);
    if (ihl < 20 || ihl + 8 > n || total < ihl + 8 || total > n ||
        (get16(p + 6) & 0x3fffu) || checksum(p, ihl) ||
        memcmp(p + 12, &st->destination, 4) || memcmp(p + 16, &st->source, 4)) return;
    const unsigned char *icmp = p + ihl;
    unsigned seq = get16(icmp + 6);
    if (icmp[0] || icmp[1] || get16(icmp + 4) != st->ping_id ||
        checksum(icmp, total - ihl) || seq >= (unsigned)st->ping_sent || st->ping_seen[seq]) return;
    st->ping_seen[seq] = 1; st->ping_received++;
    double rtt = (st->now - st->ping_times[seq]) / 1000.0;
    if (st->ping_received == 1 || rtt < st->rtt_min) st->rtt_min = rtt;
    if (rtt > st->rtt_max) st->rtt_max = rtt;
    st->rtt_sum += rtt;
    tiku_history_push(&st->rtts, st->now, (float)rtt, 1);
    char msg[160]; snprintf(msg, sizeof msg, "[ping] reply %u: %.1f ms", seq + 1, rtt);
    message(st, msg); ping_summary(st);
}

/* Text, IP packets and window frames share one wire, by the console's own
 * rules: an END is a delimiter, never a toggle; the byte after an END names
 * the channel -- 0xf1 a window frame, 0x4n an IPv4 packet -- or is text; and
 * a frame left open longer than FRAME_TTL_US is abandoned, so a stray END
 * before an ordinary letter cannot swallow the session's output. */
#define FRAME_TTL_US 30000000

static void wire_byte(console_t *st, unsigned char b)
{
    if (st->gui_frame) {
        if (b == 0xc0) { st->gui_frame = 0; st->slip_state = 1; }
        return;
    }
    if (b == 0xc0) {
        if (st->slip_state == 2 && !st->slip_bad && !st->slip_escape)
            ip_packet(st, st->packet, st->packet_n);
        st->slip_state = 1; st->packet_n = 0; st->slip_escape = st->slip_bad = 0;
        st->frame_at = st->now;
        return;
    }
    if (st->slip_state == 1) {
        /* The byte an END armed: it names a channel or it is text. */
        if (b == 0xf1) { st->gui_frame = 1; st->frame_at = st->now; return; }
        st->slip_state = b >> 4 == 4 ? 2 : 0;
        st->frame_at = st->now;
    }
    if (st->slip_state == 2) {
        if (st->slip_escape) {
            if (b == 0xdc) b = 0xc0;
            else if (b == 0xdd) b = 0xdb;
            else st->slip_bad = 1;
            st->slip_escape = 0;
        } else if (b == 0xdb) { st->slip_escape = 1; return; }
        if (st->packet_n == sizeof st->packet) st->slip_bad = 1;
        else st->packet[st->packet_n++] = b;
        return;
    }
    text_byte(st, b);
}

static void ping_start(console_t *st)
{
    if (st->state != ACTIVE || st->ping_active) return;
    char *end; long n = strtol(st->field[4].text, &end, 10);
    if (!*st->field[4].text || *end || n < 1 || n > 100 ||
        inet_pton(AF_INET, st->field[3].text, &st->destination) != 1) {
        snprintf(st->net_note, sizeof st->net_note, "Enter an IPv4 address and 1-100 probes."); return;
    }
    if (!queue_bytes(st, "slip on\r", 8)) return;
    inet_pton(AF_INET, "172.16.7.1", &st->source);
    st->ping_id++; st->ping_count = (int)n; st->ping_active = 1;
    st->ping_sent = st->ping_received = 0;
    st->rtt_sum = st->rtt_min = st->rtt_max = 0;
    memset(st->ping_seen, 0, sizeof st->ping_seen);
    tiku_history_init(&st->rtts, st->samples, 100);
    st->ping_next = st->now + 350000;
    st->plot.from_us = st->now; st->plot.to_us = st->now + 1000000;
    ping_summary(st);
}

/** A bounded train, then two seconds for replies; loss never becomes zero RTT. */
static void ping_tick(console_t *st)
{
    if (!st->ping_active || st->now < st->ping_next || st->out_at < st->out_n) return;
    if (st->ping_sent == st->ping_count) {
        st->ping_active = 0; ping_summary(st); message(st, st->net_note); return;
    }
    unsigned char p[39] = {0}, encoded[80];
    p[0] = 0x45; put16(p + 2, sizeof p); p[8] = 64; p[9] = 1;
    memcpy(p + 12, &st->source, 4); memcpy(p + 16, &st->destination, 4);
    put16(p + 10, checksum(p, 20));
    p[20] = 8; put16(p + 24, st->ping_id); put16(p + 26, (unsigned)st->ping_sent);
    memcpy(p + 28, "tikuconsole", 11); put16(p + 22, checksum(p + 20, 19));
    size_t n = slip_encode(p, sizeof p, encoded, sizeof encoded);
    if (!queue_bytes(st, encoded, n)) return;
    st->ping_times[st->ping_sent++] = st->now;
    st->ping_next = st->now + (st->ping_sent == st->ping_count ? 2000000 : 1000000);
    ping_summary(st);
}
