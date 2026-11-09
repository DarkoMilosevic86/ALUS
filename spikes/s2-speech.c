/*
 * S2 — Latencija i tačnost govora (Faza 0, 02-FAZE.md).
 *
 * Pitanje: drži li speech-dispatcher govorni ugovor koji ALUS-u treba
 * (rizik R4 iz 03-RIZICI.md), i ostaje li `SsipSynth` podrazumevani backend.
 *
 * Budžeti:
 *   speak()  -> prvi audio sample      < 50 ms
 *   cancel() -> tišina                 < 30 ms
 *   index_mark notifikacija vs zvuk    < 100 ms
 *   end notifikacija posle zvuka       < 50 ms
 *
 * Mери се STVARAN ZVUK, ne samo SSIP notifikacije: proces hvata monitor
 * podrazumevanog izlaza preko `pw-record` i detektuje početak/kraj zvuka
 * sa tačnošću od 1 ms.
 *
 * Spike kod — baca se posle Faze 0. Živi samo spikes/RESULTS.md.
 *
 * Prevod:
 *   cc -std=c11 -O2 -Wall -Wextra -o spikes/s2-speech spikes/s2-speech.c \
 *      $(pkg-config --cflags --libs speech-dispatcher) -lm
 */

#define _GNU_SOURCE
#include <libspeechd.h>

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ vreme */

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void sleep_ms(double ms)
{
	struct timespec ts = { (time_t)(ms / 1000.0),
			       (long)(fmod(ms, 1000.0) * 1e6) };
	nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------- audio tap */

#define RATE          48000
#define WIN_FRAMES    48            /* 1 ms po prozoru */
#define MAX_WIN       (12 * 60 * 1000)   /* 12 minuta */
#define READ_FRAMES   512

typedef struct {
	FILE *pipe;
	pthread_t th;
	pthread_mutex_t mu;
	volatile bool running;

	int16_t peak[MAX_WIN];      /* |amplituda| po milisekundi */
	size_t nwin;                /* broj popunjenih prozora */
	uint64_t frames_total;

	double t0;                  /* mono ms koji odgovara početku prozora 0 */
	bool t0_set;

	int16_t carry[WIN_FRAMES];  /* nedovršen prozor */
	size_t ncarry;
} AudioTap;

static AudioTap tap;

static void *audio_thread(void *arg)
{
	AudioTap *a = arg;
	int16_t buf[READ_FRAMES];

	while (a->running) {
		size_t got = fread(buf, sizeof(int16_t), READ_FRAMES, a->pipe);
		if (got == 0) {
			if (feof(a->pipe) || ferror(a->pipe))
				break;
			continue;
		}
		double t_read = now_ms();

		pthread_mutex_lock(&a->mu);
		a->frames_total += got;

		/* Procena vremenske baze: offset = t_read - (ukupno frejmova u ms).
		 * Uzimamo MINIMUM kroz sva čitanja — to je najtešnja (najmanje
		 * baferovana) procena i ispravlja drift. */
		double off = t_read - (double)a->frames_total * 1000.0 / RATE;
		if (!a->t0_set || off < a->t0) {
			a->t0 = off;
			a->t0_set = true;
		}

		/* popuni prozore od 1 ms */
		size_t i = 0;
		while (i < got) {
			size_t need = WIN_FRAMES - a->ncarry;
			size_t take = (got - i < need) ? (got - i) : need;
			memcpy(a->carry + a->ncarry, buf + i,
			       take * sizeof(int16_t));
			a->ncarry += take;
			i += take;
			if (a->ncarry == WIN_FRAMES) {
				int16_t pk = 0;
				for (size_t j = 0; j < WIN_FRAMES; j++) {
					int16_t v = a->carry[j];
					int16_t m = (v < 0) ? (int16_t)(-v) : v;
					if (m > pk)
						pk = m;
				}
				if (a->nwin < MAX_WIN)
					a->peak[a->nwin++] = pk;
				a->ncarry = 0;
			}
		}
		pthread_mutex_unlock(&a->mu);
	}
	return NULL;
}

static bool tap_start(const char *target, bool capture_sink)
{
	char cmd[512];
	const char *props = capture_sink
		? "-P 'stream.capture.sink=true'" : "";
	if (target && *target)
		snprintf(cmd, sizeof cmd,
			 "exec pw-record --raw --rate %d --channels 1 "
			 "--format s16 --latency 256 --target '%s' "
			 "%s - 2>/dev/null",
			 RATE, target, props);
	else
		snprintf(cmd, sizeof cmd,
			 "exec pw-record --raw --rate %d --channels 1 "
			 "--format s16 --latency 256 "
			 "%s - 2>/dev/null",
			 RATE, props);

	tap.pipe = popen(cmd, "r");
	if (!tap.pipe) {
		fprintf(stderr, "ne mogu da pokrenem pw-record: %s\n",
			strerror(errno));
		return false;
	}
	pthread_mutex_init(&tap.mu, NULL);
	tap.running = true;
	if (pthread_create(&tap.th, NULL, audio_thread, &tap) != 0) {
		fprintf(stderr, "pthread_create pao\n");
		return false;
	}
	return true;
}

static void tap_stop(void)
{
	tap.running = false;
	if (tap.pipe) {
		pclose(tap.pipe);      /* prekida fread */
		tap.pipe = NULL;
	}
	pthread_join(tap.th, NULL);
}

/* --- pomoćne funkcije nad omotnicom; sve rade sa indeksima prozora (= ms) */

static size_t tap_nwin(void)
{
	pthread_mutex_lock(&tap.mu);
	size_t n = tap.nwin;
	pthread_mutex_unlock(&tap.mu);
	return n;
}

static double tap_win_time(size_t k)
{
	pthread_mutex_lock(&tap.mu);
	double t = tap.t0 + (double)k;
	pthread_mutex_unlock(&tap.mu);
	return t;
}

/* prvi prozor >= from sa `consec` uzastopnih glasnih prozora */
static long tap_find_onset(size_t from, int16_t th, size_t consec,
			   double timeout_ms)
{
	double deadline = now_ms() + timeout_ms;
	size_t run = 0;
	size_t k = from;
	while (now_ms() < deadline) {
		pthread_mutex_lock(&tap.mu);
		size_t n = tap.nwin;
		while (k < n) {
			if (tap.peak[k] > th) {
				if (++run >= consec) {
					size_t start = k + 1 - consec;
					pthread_mutex_unlock(&tap.mu);
					return (long)start;
				}
			} else {
				run = 0;
			}
			k++;
		}
		pthread_mutex_unlock(&tap.mu);
		sleep_ms(1.0);
	}
	return -1;
}

/* prvi prozor >= from posle kog sledi `quiet` uzastopnih tihih prozora */
static long tap_find_silence(size_t from, int16_t th, size_t quiet,
			     double timeout_ms)
{
	double deadline = now_ms() + timeout_ms;
	size_t k = from, run = 0;
	while (now_ms() < deadline) {
		pthread_mutex_lock(&tap.mu);
		size_t n = tap.nwin;
		while (k < n) {
			if (tap.peak[k] <= th) {
				if (++run >= quiet) {
					size_t start = k + 1 - quiet;
					pthread_mutex_unlock(&tap.mu);
					return (long)start;
				}
			} else {
				run = 0;
			}
			k++;
		}
		pthread_mutex_unlock(&tap.mu);
		sleep_ms(1.0);
	}
	return -1;
}

/* poslednji glasan prozor u [from, do_) */
static long tap_last_loud(size_t from, size_t to, int16_t th)
{
	long res = -1;
	pthread_mutex_lock(&tap.mu);
	if (to > tap.nwin)
		to = tap.nwin;
	for (size_t k = from; k < to; k++)
		if (tap.peak[k] > th)
			res = (long)k;
	pthread_mutex_unlock(&tap.mu);
	return res;
}

static int16_t tap_noise_floor(size_t from, size_t to)
{
	int16_t mx = 0;
	pthread_mutex_lock(&tap.mu);
	if (to > tap.nwin)
		to = tap.nwin;
	for (size_t k = from; k < to; k++)
		if (tap.peak[k] > mx)
			mx = tap.peak[k];
	pthread_mutex_unlock(&tap.mu);
	return mx;
}

/* ------------------------------------------------------- SSIP notifikacije */

typedef struct {
	pthread_mutex_t mu;
	double t_begin, t_end, t_cancel, t_mark;
	char mark[64];
	int n_begin, n_end, n_cancel, n_mark;
	size_t last_msg_id;
} Notif;

static Notif nt;

static void notif_reset(void)
{
	pthread_mutex_lock(&nt.mu);
	nt.t_begin = nt.t_end = nt.t_cancel = nt.t_mark = 0.0;
	nt.n_begin = nt.n_end = nt.n_cancel = nt.n_mark = 0;
	nt.mark[0] = '\0';
	pthread_mutex_unlock(&nt.mu);
}

static void cb_begin(size_t msg_id, size_t client_id, SPDNotificationType st)
{
	(void)client_id; (void)st;
	pthread_mutex_lock(&nt.mu);
	nt.t_begin = now_ms();
	nt.n_begin++;
	nt.last_msg_id = msg_id;
	pthread_mutex_unlock(&nt.mu);
}

static void cb_end(size_t msg_id, size_t client_id, SPDNotificationType st)
{
	(void)msg_id; (void)client_id; (void)st;
	pthread_mutex_lock(&nt.mu);
	nt.t_end = now_ms();
	nt.n_end++;
	pthread_mutex_unlock(&nt.mu);
}

static void cb_cancel(size_t msg_id, size_t client_id, SPDNotificationType st)
{
	(void)msg_id; (void)client_id; (void)st;
	pthread_mutex_lock(&nt.mu);
	nt.t_cancel = now_ms();
	nt.n_cancel++;
	pthread_mutex_unlock(&nt.mu);
}

static void cb_im(size_t msg_id, size_t client_id, SPDNotificationType st,
		  char *index_mark)
{
	(void)msg_id; (void)client_id; (void)st;
	pthread_mutex_lock(&nt.mu);
	nt.t_mark = now_ms();
	nt.n_mark++;
	if (index_mark)
		snprintf(nt.mark, sizeof nt.mark, "%s", index_mark);
	pthread_mutex_unlock(&nt.mu);
}

static double notif_wait(double *field, int *counter, int want,
			 double timeout_ms)
{
	double deadline = now_ms() + timeout_ms;
	while (now_ms() < deadline) {
		pthread_mutex_lock(&nt.mu);
		bool ok = (*counter >= want);
		double t = *field;
		pthread_mutex_unlock(&nt.mu);
		if (ok)
			return t;
		sleep_ms(1.0);
	}
	return -1.0;
}

/* ------------------------------------------------------------- statistika */

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

typedef struct {
	const char *name;
	const char *budget_txt;
	double budget;
	double p50, p95, p99, mn, mx, mean;
	int n;
	bool have_budget;
} Stat;

static Stat compute(const char *name, double *v, int n, double budget,
		    bool have_budget, const char *budget_txt)
{
	Stat s = { 0 };
	s.name = name;
	s.budget = budget;
	s.have_budget = have_budget;
	s.budget_txt = budget_txt;
	s.n = n;
	if (n == 0)
		return s;
	double *c = malloc((size_t)n * sizeof(double));
	memcpy(c, v, (size_t)n * sizeof(double));
	qsort(c, (size_t)n, sizeof(double), cmp_double);
	double sum = 0;
	for (int i = 0; i < n; i++)
		sum += c[i];
	s.mean = sum / n;
	s.mn = c[0];
	s.mx = c[n - 1];
#define PCT(p) c[(int)((p) / 100.0 * n + 0.5) - 1 < 0 ? 0 : \
		  ((int)((p) / 100.0 * n + 0.5) - 1 >= n ? n - 1 : \
		   (int)((p) / 100.0 * n + 0.5) - 1)]
	s.p50 = PCT(50);
	s.p95 = PCT(95);
	s.p99 = PCT(99);
#undef PCT
	free(c);
	return s;
}

static void print_stat(Stat s)
{
	if (s.n == 0) {
		printf("  %-34s  nema uzoraka\n", s.name);
		return;
	}
	const char *v = "";
	if (s.have_budget)
		v = (s.p95 < s.budget) ? "  PASS" : "  FAIL";
	printf("  %-34s n=%-4d p50=%8.2f  p95=%8.2f  p99=%8.2f  "
	       "min=%8.2f  max=%8.2f ms   budžet %-8s%s\n",
	       s.name, s.n, s.p50, s.p95, s.p99, s.mn, s.mx,
	       s.have_budget ? s.budget_txt : "—", v);
}

/* ------------------------------------------------------------------- main */

static const char *TXT_SHORT =
	"Један два три четири пет.";
static const char *TXT_LONG =
	"Ово је дугачка проба за мерење времена прекида говора. "
	"Текст мора да траје довољно дуго да прекид падне у средину "
	"изговора, јер се тада мери стварно време до тишине.";
static const char *SSML_MARK =
	"<speak>Први део текста пре ознаке."
	"<break time=\"700ms\"/><mark name=\"m1\"/>"
	"Други део текста после ознаке.</speak>";

static volatile sig_atomic_t stop_now = 0;
static void on_sigint(int s) { (void)s; stop_now = 1; }

int main(int argc, char **argv)
{
	int iterations = 100;
	int mark_iters = 20;
	const char *module = "espeak-ng";
	const char *language = "sr";
	const char *target = NULL;
	const char *json_path = NULL;
	int rate = 0;
	bool capture_sink = true;
	int warmup = 3;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--iterations") && i + 1 < argc)
			iterations = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--mark-iterations") && i + 1 < argc)
			mark_iters = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--module") && i + 1 < argc)
			module = argv[++i];
		else if (!strcmp(argv[i], "--language") && i + 1 < argc)
			language = argv[++i];
		else if (!strcmp(argv[i], "--target") && i + 1 < argc)
			target = argv[++i];
		else if (!strcmp(argv[i], "--json") && i + 1 < argc)
			json_path = argv[++i];
		else if (!strcmp(argv[i], "--rate") && i + 1 < argc)
			rate = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--no-capture-sink"))
			capture_sink = false;
		else if (!strcmp(argv[i], "--warmup") && i + 1 < argc)
			warmup = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--report"))
			; /* подразумевано */
		else {
			fprintf(stderr,
				"upotreba: %s [--iterations N] [--mark-iterations N]\n"
				"          [--module NAME] [--language LL] [--target SINK]\n"
				"          [--rate -100..100] [--warmup N]\n"
				"          [--no-capture-sink] [--json FILE]\n"
				"\n--no-capture-sink: hvata IZLAZNI STREAM datog --target-a\n"
				"  (npr. speech-dispatcher-espeak-ng) umesto monitora sink-a;\n"
				"  tako se meri pipeline bez latencije audio uređaja.\n",
				argv[0]);
			return 2;
		}
	}

	setvbuf(stdout, NULL, _IOLBF, 0);   /* da log teče i kad je stdout fajl */
	signal(SIGINT, on_sigint);
	pthread_mutex_init(&nt.mu, NULL);

	printf("==========================================================================\n");
	printf("S2 — latencija i tačnost govora | modul=%s jezik=%s iteracija=%d\n",
	       module, language, iterations);
	printf("==========================================================================\n");

	/* --- audio tap --- */
	if (!tap_start(target, capture_sink))
		return 1;
	printf("audio tap: pw-record %s %s\n",
	       capture_sink ? "na monitor" : "na izlazni stream",
	       target ? target : "podrazumevanog izlaza");
	sleep_ms(700);          /* pusti stream da se poveže */
	if (!tap.t0_set) {
		fprintf(stderr, "GREŠKA: nijedan audio frejm nije pročitan. "
			"Da li pw-record radi?\n");
		tap_stop();
		return 1;
	}

	/* --- SSIP --- */
	char *err = NULL;
	SPDConnection *conn = spd_open2("alus-s2", "spike", NULL,
					SPD_MODE_THREADED, NULL, 1, &err);
	if (!conn) {
		fprintf(stderr, "spd_open2 pao: %s\n", err ? err : "?");
		tap_stop();
		return 1;
	}
	conn->callback_begin = cb_begin;
	conn->callback_end = cb_end;
	conn->callback_cancel = cb_cancel;
	conn->callback_im = cb_im;
	if (spd_set_notification_on(conn, SPD_BEGIN) < 0 ||
	    spd_set_notification_on(conn, SPD_END) < 0 ||
	    spd_set_notification_on(conn, SPD_CANCEL) < 0 ||
	    spd_set_notification_on(conn, SPD_INDEX_MARKS) < 0)
		fprintf(stderr, "UPOZORENJE: neka notifikacija nije uključena\n");

	if (spd_set_output_module(conn, module) < 0)
		fprintf(stderr, "UPOZORENJE: modul %s nije postavljen\n", module);
	if (spd_set_language(conn, language) < 0)
		fprintf(stderr, "UPOZORENJE: jezik %s nije postavljen\n", language);
	if (rate && spd_set_voice_rate(conn, rate) < 0)
		fprintf(stderr, "UPOZORENJE: brzina nije postavljena\n");
	printf("SSIP: client_id=%d, notifikacije BEGIN/END/CANCEL/INDEX_MARKS\n",
	       spd_get_client_id(conn));

	/* --- šumni pod --- */
	spd_cancel_all(conn);
	sleep_ms(600);
	size_t w = tap_nwin();
	int16_t noise = tap_noise_floor(w > 400 ? w - 400 : 0, w);
	int16_t th = (int16_t)(noise * 4 > 250 ? noise * 4 : 250);
	printf("šumni pod: %d, prag detekcije: %d\n", noise, th);
	if (noise > 2000)
		printf("UPOZORENJE: šumni pod je visok — nešto drugo pušta zvuk "
		       "(Orca?). Merenja će biti netačna.\n");

	double *onset = calloc((size_t)iterations, sizeof(double));
	double *begin = calloc((size_t)iterations, sizeof(double));
	double *endev = calloc((size_t)iterations, sizeof(double));
	double *cancl = calloc((size_t)iterations, sizeof(double));
	double *cnotif = calloc((size_t)iterations, sizeof(double));
	double *markdev = calloc((size_t)mark_iters, sizeof(double));
	int n_onset = 0, n_begin = 0, n_end = 0, n_cancel = 0, n_cnotif = 0,
	    n_mark = 0, n_mark_fail = 0;

	/* ===== warm-up: prvi poziv učitava modul i glas, i nije merljiv ===== */
	if (warmup > 0) {
		printf("\nwarm-up: %d izgovora (prvi poziv učitava modul/glas)\n",
		       warmup);
		for (int i = 0; i < warmup && !stop_now; i++) {
			notif_reset();
			if (spd_say(conn, SPD_MESSAGE, TXT_SHORT) < 0)
				continue;
			notif_wait(&nt.t_end, &nt.n_end, 1, 8000);
			sleep_ms(120);
		}
		spd_cancel_all(conn);
		sleep_ms(300);
	}

	/* ===== FAZA A: speak() -> prvi sample, i end notifikacija ===== */
	printf("\n--- A  speak() → prvi audio sample, i `end` vs kraj zvuka (%d iter.) ---\n",
	       iterations);
	for (int i = 0; i < iterations && !stop_now; i++) {
		/* čekaj tišinu */
		size_t w0 = tap_nwin();
		if (tap_find_silence(w0, th, 60, 2000) < 0) {
			/* nije tiho — preskoči iteraciju */
		}
		w0 = tap_nwin();
		notif_reset();

		double t_speak = now_ms();
		int msg = spd_say(conn, SPD_MESSAGE, TXT_SHORT);
		if (msg < 0) {
			fprintf(stderr, "spd_say pao na iteraciji %d\n", i);
			continue;
		}
		long k = tap_find_onset(w0, th, 3, 4000);
		if (k < 0) {
			fprintf(stderr, "  iter %d: zvuk se nije pojavio\n", i);
			spd_cancel_all(conn);
			sleep_ms(200);
			continue;
		}
		double t_onset = tap_win_time((size_t)k);
		onset[n_onset++] = t_onset - t_speak;

		double t_b = notif_wait(&nt.t_begin, &nt.n_begin, 1, 100);
		if (t_b > 0)
			begin[n_begin++] = t_b - t_speak;

		double t_e = notif_wait(&nt.t_end, &nt.n_end, 1, 8000);
		if (t_e > 0) {
			/* poslednji glasan prozor pre notifikacije */
			size_t wend = tap_nwin();
			long last = tap_last_loud((size_t)k, wend, th);
			if (last >= 0) {
				double t_last = tap_win_time((size_t)last);
				endev[n_end++] = t_e - t_last;
			}
			/* Napomena: `last` je zadnji ČUJNI prozor. espeak dodaje
			 * tišinu na kraj izgovora, pa ovaj broj sadrži i nju —
			 * vidi RESULTS.md nalaz o `end` notifikaciji. */
		}
		if ((i + 1) % 20 == 0)
			printf("    %d/%d\n", i + 1, iterations);
	}

	/* ===== FAZA B: cancel() -> tišina ===== */
	printf("\n--- B  cancel() → tišina (%d iter.) ---\n", iterations);
	for (int i = 0; i < iterations && !stop_now; i++) {
		spd_cancel_all(conn);
		size_t w0 = tap_nwin();
		tap_find_silence(w0, th, 60, 2000);
		w0 = tap_nwin();
		notif_reset();

		if (spd_say(conn, SPD_MESSAGE, TXT_LONG) < 0)
			continue;
		long k = tap_find_onset(w0, th, 3, 4000);
		if (k < 0) {
			spd_cancel_all(conn);
			continue;
		}
		/* pusti govor da traje ~400 ms pa prekini u sredini */
		sleep_ms(400);
		size_t wc = tap_nwin();
		double t_cancel = now_ms();
		spd_cancel(conn);

		long s = tap_find_silence(wc, th, 25, 3000);
		if (s >= 0) {
			double t_sil = tap_win_time((size_t)s);
			cancl[n_cancel++] = t_sil - t_cancel;
		}
		double t_c = notif_wait(&nt.t_cancel, &nt.n_cancel, 1, 500);
		if (t_c > 0)
			cnotif[n_cnotif++] = t_c - t_cancel;
		if ((i + 1) % 20 == 0)
			printf("    %d/%d\n", i + 1, iterations);
	}

	/* ===== FAZA C: index_mark vs stvarna pozicija u zvuku ===== */
	printf("\n--- C  index_mark notifikacija vs pozicija u zvuku (%d iter.) ---\n",
	       mark_iters);
	if (spd_set_data_mode(conn, SPD_DATA_SSML) < 0)
		fprintf(stderr, "UPOZORENJE: SSML mod nije postavljen\n");

	for (int i = 0; i < mark_iters && !stop_now; i++) {
		spd_cancel_all(conn);
		size_t w0 = tap_nwin();
		tap_find_silence(w0, th, 60, 2000);
		w0 = tap_nwin();
		notif_reset();

		if (spd_say(conn, SPD_MESSAGE, SSML_MARK) < 0)
			continue;
		long k = tap_find_onset(w0, th, 3, 4000);
		if (k < 0) {
			spd_cancel_all(conn);
			continue;
		}
		/* nađi pauzu >= 400 ms posle početka, pa prvi zvuk posle nje:
		 * to je stvarna pozicija oznake u zvuku */
		long gap = tap_find_silence((size_t)k + 50, th, 400, 6000);
		long resume = (gap < 0) ? -1
			: tap_find_onset((size_t)gap + 400, th, 3, 6000);
		double t_mark = notif_wait(&nt.t_mark, &nt.n_mark, 1, 6000);

		if (t_mark > 0 && resume >= 0) {
			double t_res = tap_win_time((size_t)resume);
			markdev[n_mark++] = t_mark - t_res;
		} else {
			n_mark_fail++;
			if (t_mark <= 0)
				fprintf(stderr, "  iter %d: nema INDEX MARK "
					"notifikacije\n", i);
			else
				fprintf(stderr, "  iter %d: pauza u zvuku nije "
					"nađena (gap=%ld resume=%ld)\n",
					i, gap, resume);
		}
		notif_wait(&nt.t_end, &nt.n_end, 1, 8000);
	}
	spd_set_data_mode(conn, SPD_DATA_TEXT);
	spd_cancel_all(conn);

	/* ===== izveštaj ===== */
	printf("\n==========================================================================\n");
	printf("REZULTATI (modul %s)\n", module);
	printf("==========================================================================\n");
	Stat s_onset = compute("speak() → prvi audio sample", onset, n_onset,
			       50.0, true, "< 50 ms");
	Stat s_begin = compute("speak() → `begin` notifikacija", begin, n_begin,
			       0, false, NULL);
	Stat s_cancl = compute("cancel() → tišina", cancl, n_cancel,
			       30.0, true, "< 30 ms");
	Stat s_cnot = compute("cancel() → `cancel` notifikacija", cnotif,
			      n_cnotif, 0, false, NULL);
	Stat s_end = compute("`end` notifikacija posle zvuka", endev, n_end,
			     50.0, true, "< 50 ms");

	/* za index mark: apsolutno odstupanje */
	double *absdev = calloc((size_t)(n_mark ? n_mark : 1), sizeof(double));
	for (int i = 0; i < n_mark; i++)
		absdev[i] = fabs(markdev[i]);
	Stat s_mark = compute("|index_mark − pozicija u zvuku|", absdev, n_mark,
			      100.0, true, "< 100 ms");

	print_stat(s_onset);
	print_stat(s_begin);
	print_stat(s_cancl);
	print_stat(s_cnot);
	print_stat(s_end);
	print_stat(s_mark);

	if (n_mark) {
		double sum = 0;
		for (int i = 0; i < n_mark; i++)
			sum += markdev[i];
		printf("\n  index_mark predznak: srednje odstupanje %+.1f ms "
		       "(%s zvuka)\n", sum / n_mark,
		       sum / n_mark < 0 ? "notifikacija PRE" : "notifikacija POSLE");
	}
	if (n_mark_fail)
		printf("  index_mark: %d iteracija neuspešno\n", n_mark_fail);

	bool pass = (n_onset && s_onset.p95 < 50.0) &&
		    (n_cancel && s_cancl.p95 < 30.0) &&
		    (n_end && s_end.p95 < 50.0) &&
		    (n_mark && s_mark.p95 < 100.0);
	printf("\n==========================================================================\n");
	printf("ZAKLJUČAK: %s\n", pass ? "SSIP drži sva četiri budžeta → PASS"
				       : "najmanje jedan budžet prekoračen → FAIL");
	printf("==========================================================================\n");

	if (json_path) {
		FILE *f = fopen(json_path, "w");
		if (f) {
			fprintf(f, "{\n  \"module\": \"%s\",\n  \"language\": \"%s\",\n",
				module, language);
			fprintf(f, "  \"iterations\": %d,\n  \"noise_floor\": %d,\n"
				"  \"threshold\": %d,\n", iterations, noise, th);
			struct { const char *k; Stat s; } rows[] = {
				{ "onset", s_onset }, { "begin_notif", s_begin },
				{ "cancel_silence", s_cancl },
				{ "cancel_notif", s_cnot },
				{ "end_notif", s_end }, { "index_mark_abs", s_mark },
			};
			for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
				Stat x = rows[i].s;
				fprintf(f, "  \"%s\": {\"n\": %d, \"p50\": %.3f, "
					"\"p95\": %.3f, \"p99\": %.3f, \"min\": %.3f, "
					"\"max\": %.3f, \"mean\": %.3f}%s\n",
					rows[i].k, x.n, x.p50, x.p95, x.p99,
					x.mn, x.mx, x.mean,
					i + 1 < sizeof rows / sizeof rows[0] ? "," : ",");
			}
			fprintf(f, "  \"verdict\": \"%s\"\n}\n", pass ? "PASS" : "FAIL");
			fclose(f);
			printf("sirovi brojevi: %s\n", json_path);
		}
	}

	free(onset); free(begin); free(endev); free(cancl); free(cnotif);
	free(markdev); free(absdev);
	spd_close(conn);
	tap_stop();
	return pass ? 0 : 1;
}
