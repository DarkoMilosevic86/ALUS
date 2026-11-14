/*
 * S2b — Direktni `libespeak-ng` u AUDIO_OUTPUT_SYNCHRONOUS modu.
 *
 * Poređenje koje phase-plan.md traži za S2: da li `EspeakDirectSynth` nosi
 * prednost koja opravdava drugi backend, ili `SsipSynth` ostaje sam.
 *
 * Šta se meri — i šta se NE meri:
 *
 *   U SYNCHRONOUS modu espeak ne pušta zvuk. Daje PCM blokove i događaje
 *   kroz callback, a aplikacija (ALUS) sama vlada izlazom. Zato se ovde NE
 *   može izmeriti „speak() → prvi audio sample na zvučnoj kartici" — taj broj
 *   zavisi od ALUS-ovog izlaznog bafera, koji ALUS bira.
 *
 *   Meri se ono što je svojstvo sintetizatora:
 *     1. espeak_Synth() → prvi PCM blok   (latencija sinteze)
 *     2. ukupna sinteza / trajanje zvuka  (real-time faktor)
 *     3. tačnost `mark` događaja          (da li je uzorak-tačna)
 *
 *   Za (3): u SYNCHRONOUS modu `espeak_EVENT` nosi `audio_position` u ms i
 *   `sample`. Ako `audio_position` pada unutar bloka koji je isporučen u istom
 *   callback-u, pozicija je uzorak-tačna po konstrukciji — greška je 0, a ne
 *   40 ms kao kod SSIP-a (vidi RESULTS.md N13).
 *
 * Spike kod — baca se posle Faze 0.
 *
 * Prevod:
 *   cc -std=c11 -O2 -Wall -Wextra -o spikes/s2-espeak spikes/s2-espeak.c \
 *      $(pkg-config --cflags --libs espeak-ng) -lm
 */

#define _GNU_SOURCE
#include <espeak-ng/speak_lib.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* --- stanje jedne iteracije, deljeno sa callback-om --- */
static struct {
	double t_start;
	double t_first_block;
	bool have_first;
	long total_samples;
	int sample_rate;

	int n_blocks;
	int n_marks;
	/* za svaki mark: da li je audio_position unutar isporučenog bloka */
	int marks_in_block;
	int marks_outside;
	double mark_err_ms[64];
	int n_mark_err;
	int last_mark_pos_ms;
} it;

static int synth_cb(short *wav, int numsamples, espeak_EVENT *events)
{
	double t = now_ms();

	/* događaji stižu uz blok; LIST_TERMINATED zatvara listu */
	long block_start = it.total_samples;
	long block_end = block_start + (numsamples > 0 ? numsamples : 0);
	double start_ms = (double)block_start * 1000.0 / it.sample_rate;
	double end_ms = (double)block_end * 1000.0 / it.sample_rate;

	for (espeak_EVENT *e = events; e && e->type != espeakEVENT_LIST_TERMINATED;
	     e++) {
		if (e->type != espeakEVENT_MARK)
			continue;
		it.n_marks++;
		it.last_mark_pos_ms = e->audio_position;
		double ap = (double)e->audio_position;
		if (ap >= start_ms - 0.001 && ap <= end_ms + 0.001) {
			it.marks_in_block++;
			if (it.n_mark_err < 64)
				it.mark_err_ms[it.n_mark_err++] = 0.0;
		} else {
			it.marks_outside++;
			/* koliko je promašio blok */
			double err = (ap < start_ms) ? (start_ms - ap) : (ap - end_ms);
			if (it.n_mark_err < 64)
				it.mark_err_ms[it.n_mark_err++] = err;
		}
	}

	if (wav == NULL)
		return 0;              /* kraj sinteze */
	if (numsamples > 0) {
		if (!it.have_first) {
			it.t_first_block = t;
			it.have_first = true;
		}
		it.total_samples += numsamples;
		it.n_blocks++;
	}
	return 0;                      /* 0 = nastavi */
}

/* --- statistika --- */
static int cmpd(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

static void report(const char *name, double *v, int n, const char *unit)
{
	if (n == 0) {
		printf("  %-36s nema uzoraka\n", name);
		return;
	}
	double *c = malloc((size_t)n * sizeof(double));
	memcpy(c, v, (size_t)n * sizeof(double));
	qsort(c, (size_t)n, sizeof(double), cmpd);
#define P(p) c[((int)((p) / 100.0 * n + 0.5) - 1 < 0) ? 0 : \
	       (((int)((p) / 100.0 * n + 0.5) - 1 >= n) ? n - 1 : \
		(int)((p) / 100.0 * n + 0.5) - 1)]
	double sum = 0;
	for (int i = 0; i < n; i++)
		sum += c[i];
	printf("  %-36s n=%-4d p50=%8.2f  p95=%8.2f  p99=%8.2f  "
	       "min=%8.2f  max=%8.2f  %s\n",
	       name, n, P(50), P(95), P(99), c[0], c[n - 1], unit);
	if (strstr(unit, "\u00d7"))
		printf("  %-36s        p50=%8.5f  p95=%8.5f  "
		       "(sinteza je %.0f\u00d7 brza od realnog vremena)\n",
		       "", P(50), P(95), P(50) > 0 ? 1.0 / P(50) : 0.0);
#undef P
	free(c);
}

static const char *TXT =
	"Један два три четири пет.";
/* NALAZ (N15): espeak-ng TIHO IZGUBI `<mark>` koji stoji posle granice
 * recenice kada je klauza pre njega dovoljno dugacka. Bez greske, bez
 * dogadjaja. Oblici koji rade:
 *   - mark bez tacke pre njega
 *   - mark na SAMOM KRAJU izgovora (ovo je oblik koji ALUS koristi za
 *     ulancavanje, vidi N12)
 * Oblik koji pada:
 *   - "<speak>dugacka klauza. <mark/> ostatak</speak>"
 * Zato ovde merimo oba: jedan mark na kraju (radi) i jedan u sredini bez
 * tacke (radi), pa da tacnost pozicije uopste bude merljiva.
 */
static const char *TXT_SSML =
	"<speak>Први део текста пре ознаке <mark name=\"m1\"/> "
	"други део текста после ознаке<mark name=\"m2\"/></speak>";

int main(int argc, char **argv)
{
	int iterations = 100;
	const char *voice = "sr";
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--iterations") && i + 1 < argc)
			iterations = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--voice") && i + 1 < argc)
			voice = argv[++i];
		else {
			fprintf(stderr, "upotreba: %s [--iterations N] [--voice V]\n",
				argv[0]);
			return 2;
		}
	}
	setvbuf(stdout, NULL, _IOLBF, 0);

	printf("==========================================================================\n");
	printf("S2b — direktni libespeak-ng, AUDIO_OUTPUT_SYNCHRONOUS\n");
	printf("==========================================================================\n");

	/* buflength u ms; 0 = podrazumevano (60 ms). Birамо 10 ms, jer bi to
	 * ALUS i koristio da drži nizak izlazni bafer. */
	int rate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 10, NULL, 0);
	if (rate <= 0) {
		fprintf(stderr, "espeak_Initialize pao (%d)\n", rate);
		return 1;
	}
	it.sample_rate = rate;
	printf("sample rate: %d Hz, buflength 10 ms\n", rate);
	espeak_SetSynthCallback(synth_cb);

	if (espeak_SetVoiceByName(voice) != EE_OK) {
		fprintf(stderr, "UPOZORENJE: glas %s nije postavljen, ostaje podrazumevani\n",
			voice);
	} else {
		printf("glas: %s\n", voice);
	}
	printf("espeak verzija: %s\n", espeak_Info(NULL));

	double *lat = calloc((size_t)iterations, sizeof(double));
	double *tot = calloc((size_t)iterations, sizeof(double));
	double *rtf = calloc((size_t)iterations, sizeof(double));
	double *dur = calloc((size_t)iterations, sizeof(double));
	int n = 0;

	/* warm-up */
	for (int i = 0; i < 3; i++) {
		memset(&it, 0, sizeof it);
		it.sample_rate = rate;
		it.t_start = now_ms();
		espeak_Synth(TXT, strlen(TXT) + 1, 0, POS_CHARACTER, 0,
			     espeakCHARS_UTF8, NULL, NULL);
	}

	printf("\n--- A  latencija sinteze i real-time faktor (%d iter.) ---\n",
	       iterations);
	for (int i = 0; i < iterations; i++) {
		memset(&it, 0, sizeof it);
		it.sample_rate = rate;
		it.t_start = now_ms();
		espeak_ERROR err = espeak_Synth(TXT, strlen(TXT) + 1, 0,
						POS_CHARACTER, 0,
						espeakCHARS_UTF8, NULL, NULL);
		double t_done = now_ms();
		if (err != EE_OK || !it.have_first) {
			fprintf(stderr, "  iter %d: espeak_Synth err=%d blokova=%d\n",
				i, err, it.n_blocks);
			continue;
		}
		double audio_ms = (double)it.total_samples * 1000.0 / rate;
		lat[n] = it.t_first_block - it.t_start;
		tot[n] = t_done - it.t_start;
		dur[n] = audio_ms;
		rtf[n] = (audio_ms > 0) ? (t_done - it.t_start) / audio_ms : 0;
		n++;
	}

	printf("\n--- B  tačnost `mark` događaja (%d iter., SSML) ---\n", iterations);
	int marks_total = 0, marks_in = 0, marks_out = 0, iters_with_mark = 0;
	double worst_err = 0;
	for (int i = 0; i < iterations; i++) {
		memset(&it, 0, sizeof it);
		it.sample_rate = rate;
		it.t_start = now_ms();
		espeak_ERROR err = espeak_Synth(TXT_SSML, strlen(TXT_SSML) + 1, 0,
						POS_CHARACTER, 0,
						espeakCHARS_UTF8 | espeakSSML,
						NULL, NULL);
		if (err != EE_OK)
			continue;
		marks_total += it.n_marks;
		marks_in += it.marks_in_block;
		marks_out += it.marks_outside;
		if (it.n_marks > 0)
			iters_with_mark++;
		for (int k = 0; k < it.n_mark_err; k++)
			if (it.mark_err_ms[k] > worst_err)
				worst_err = it.mark_err_ms[k];
	}

	printf("\n==========================================================================\n");
	printf("REZULTATI — direktni libespeak-ng\n");
	printf("==========================================================================\n");
	report("espeak_Synth() → prvi PCM blok", lat, n, "ms");
	report("ukupna sinteza izgovora", tot, n, "ms");
	report("trajanje zvuka izgovora", dur, n, "ms");
	report("real-time faktor (sinteza/zvuk)", rtf, n, "×");

	printf("\n  `mark` događaji: %d ukupno u %d/%d iteracija\n",
	       marks_total, iters_with_mark, iterations);
	printf("  pozicija unutar isporučenog bloka: %d\n", marks_in);
	printf("  pozicija IZVAN bloka:              %d\n", marks_out);
	printf("  najveće odstupanje:                %.3f ms\n", worst_err);
	if (marks_total > 0 && marks_out == 0)
		printf("  → `mark` je UZORAK-TAČAN: audio_position uvek pada u blok\n"
		       "    koji je isporučen u istom callback-u. Greška je 0 ms,\n"
		       "    nasuprot postojanom pomeraju od −40 ms kod SSIP-a (N13).\n");
	else if (marks_total == 0)
		printf("  → nijedan `mark` nije stigao — SSML nije obrađen?\n");

	printf("\n  NAPOMENA: `cancel() → tišina` se ovde ne meri. U SYNCHRONOUS\n");
	printf("  modu ALUS vlada izlazom, pa je prekid ograničen SAMO ALUS-ovim\n");
	printf("  izlaznim baferom (ovde 10 ms), a ne sintetizatorom.\n");
	printf("  Kod SSIP-a je izmereno 26.5 ms p50 / 29.0 ms p95, sa repom od 215 ms.\n");
	printf("==========================================================================\n");

	free(lat); free(tot); free(rtf); free(dur);
	espeak_Terminate();
	return 0;
}
