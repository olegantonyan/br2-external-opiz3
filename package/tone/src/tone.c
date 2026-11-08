#include <alsa/asoundlib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static unsigned gcd(unsigned a, unsigned b)
{
	while (b) {
		unsigned t = a % b;
		a = b;
		b = t;
	}
	return a;
}

static void usage(void)
{
	fprintf(stderr, "usage: tone [-D device] [-f hz] [-r rate] [-a dBFS] [-b 16|24] [-s seconds]\n");
	exit(2);
}

int main(int argc, char **argv)
{
	const char *dev = "hw:SPDIF";
	unsigned freq = 440, rate = 48000, bits = 16;
	double dbfs = -6, seconds = 0;
	int opt;

	while ((opt = getopt(argc, argv, "D:f:r:a:b:s:h")) != -1) {
		switch (opt) {
		case 'D': dev = optarg; break;
		case 'f': freq = strtoul(optarg, NULL, 10); break;
		case 'r': rate = strtoul(optarg, NULL, 10); break;
		case 'a': dbfs = atof(optarg); break;
		case 'b': bits = strtoul(optarg, NULL, 10); break;
		case 's': seconds = atof(optarg); break;
		default: usage();
		}
	}
	if (!freq || !rate || freq >= rate / 2 || dbfs > 0 || (bits != 16 && bits != 24))
		usage();

	unsigned period = rate / gcd(freq, rate);
	unsigned cycles = freq / gcd(freq, rate);
	double amp = ((1 << (bits - 1)) - 1) * pow(10, dbfs / 20);
	snd_pcm_format_t fmt = bits == 16 ? SND_PCM_FORMAT_S16_LE : SND_PCM_FORMAT_S32_LE;
	size_t frame = bits == 16 ? 2 * sizeof(int16_t) : 2 * sizeof(int32_t);
	char *buf = malloc(period * frame);
	if (!buf)
		return 1;
	for (unsigned i = 0; i < period; i++) {
		long v = lrint(amp * sin(2 * M_PI * cycles * i / period));
		if (bits == 16) {
			int16_t *f = (int16_t *)buf + 2 * i;
			f[0] = f[1] = v;
		} else {
			int32_t *f = (int32_t *)buf + 2 * i;
			f[0] = f[1] = (int32_t)((uint32_t)v << 8);
		}
	}

	snd_pcm_t *pcm;
	int err = snd_pcm_open(&pcm, dev, SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) {
		fprintf(stderr, "%s: %s\n", dev, snd_strerror(err));
		return 1;
	}
	err = snd_pcm_set_params(pcm, fmt, SND_PCM_ACCESS_RW_INTERLEAVED, 2, rate, 0, 200000);
	if (err < 0) {
		fprintf(stderr, "set_params: %s\n", snd_strerror(err));
		return 1;
	}

	fprintf(stderr, "%s: %u Hz sine, %.1f dBFS, %u Hz %u-bit (%s) stereo, %u-frame loop\n", dev, freq, dbfs, rate, bits, snd_pcm_format_name(fmt), period);

	unsigned long long left = seconds > 0 ? (unsigned long long)(seconds * rate) : 0;
	for (;;) {
		snd_pcm_uframes_t n = period;
		if (seconds > 0) {
			if (!left)
				break;
			if (n > left)
				n = left;
		}
		char *p = buf;
		while (n) {
			snd_pcm_sframes_t w = snd_pcm_writei(pcm, p, n);
			if (w < 0) {
				if (w == -EPIPE)
					fprintf(stderr, "underrun\n");
				if (snd_pcm_recover(pcm, w, 0) < 0) {
					fprintf(stderr, "write: %s\n", snd_strerror(w));
					return 1;
				}
				continue;
			}
			p += w * frame;
			n -= w;
			if (seconds > 0)
				left -= w;
		}
	}
	snd_pcm_drain(pcm);
	snd_pcm_close(pcm);
	return 0;
}
