/*
 * S2c — Detektor za rizik R14: tiho izgubljen `index mark`.
 *
 * Faza 0 je otkrila da espeak-ng izgubi `<mark>` koji stoji posle granice
 * rečenice kada je klauza pre njega dovoljno dugačka (RESULTS.md N15), i da
 * SSIP putanja to nasleđuje (N16). Nema greške — oznaka prosto ne dođe.
 *
 * Posledica je kritična: `SpeechManager` koji prati napredak preko index
 * marks zablokira čekajući indeks koji nikada ne stiže.
 *
 * Ovo je **detektor** iz registra rizika: pokrenuti ga na svakom modulu koji
 * ALUS podržava, i pri svakoj nadogradnji speech-dispatcher-a ili espeak-ng-a.
 * Izlazni kod 0 = svi markovi stigli; 1 = najmanje jedan izgubljen.
 *
 * Prevod:
 *   cc -std=c11 -O2 -Wall -Wextra -o spikes/s2-markloss spikes/s2-markloss.c \
 *      $(pkg-config --cflags --libs speech-dispatcher) -lm
 * Upotreba:
 *   ./spikes/s2-markloss              # espeak-ng (podrazumevano)
 *   ./spikes/s2-markloss rhvoice      # drugi modul
 */

#define _GNU_SOURCE
#include <libspeechd.h>

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int n_mark, n_end;
static char got[256];

static void cb_im(size_t m, size_t c, SPDNotificationType s, char *name){
  (void)m;(void)c;(void)s;
  pthread_mutex_lock(&mu);
  n_mark++;
  if(name){ strncat(got,name,sizeof got-strlen(got)-2); strncat(got," ",sizeof got-strlen(got)-2); }
  pthread_mutex_unlock(&mu);
}
static void cb_end(size_t m, size_t c, SPDNotificationType s){
  (void)m;(void)c;(void)s;
  pthread_mutex_lock(&mu); n_end++; pthread_mutex_unlock(&mu);
}
static double now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
  return t.tv_sec*1000.0+t.tv_nsec/1e6; }
static void nap(double ms){ struct timespec t={(time_t)(ms/1000),(long)(fmod(ms,1000)*1e6)};
  nanosleep(&t,NULL); }

int main(int argc, char **argv){
  const char *module = (argc > 1) ? argv[1] : "espeak-ng";
  char *err=NULL;
  SPDConnection *c = spd_open2("alus-markdiag","diag",NULL,SPD_MODE_THREADED,NULL,1,&err);
  if(!c){ fprintf(stderr,"spd_open2 pao: %s\n", err?err:"?"); return 1; }
  c->callback_im = cb_im; c->callback_end = cb_end;
  spd_set_notification_on(c, SPD_INDEX_MARKS);
  spd_set_notification_on(c, SPD_END);
  if(spd_set_output_module(c, module) < 0)
    fprintf(stderr, "UPOZORENJE: modul %s nije postavljen\n", module);
  spd_set_language(c, "sr");
  spd_set_data_mode(c, SPD_DATA_SSML);

  struct { const char *label; const char *txt; int expect; } T[] = {
    {"kratko, mark bez tacke",
     "<speak>Prvi deo <mark name=\"m\"/> drugi deo.</speak>", 1},
    {"kratko, mark posle tacke",
     "<speak>Prvi deo. <mark name=\"m\"/> drugi deo.</speak>", 1},
    {"DUGO, mark posle tacke  (pada u direktnom espeak-u)",
     "<speak>Ovo je duzi prvi deo teksta pre oznake koji ima vise reci. "
     "<mark name=\"m\"/> A ovo je drugi deo.</speak>", 1},
    {"DUGO, mark bez tacke",
     "<speak>Ovo je duzi prvi deo teksta pre oznake koji ima vise reci "
     "<mark name=\"m\"/> a ovo je drugi deo.</speak>", 1},
    {"mark na samom kraju posle tacke (ulancavanje!)",
     "<speak>Ovo je recenica kakvu bi ALUS zaista izgovorio, sa vise reci."
     "<mark name=\"kraj\"/></speak>", 1},
    {"dva marka, dugacke klauze",
     "<speak><mark name=\"a\"/>Prva dugacka recenica sa vise reci u njoj. "
     "<mark name=\"b\"/>Druga dugacka recenica sa vise reci.</speak>", 2},
    {"cirilica, mark na kraju posle tacke",
     "<speak>Ово је реченица какву би ALUS заиста изговорио, са више речи."
     "<mark name=\"kraj\"/></speak>", 1},
  };

  printf("SSIP index marks kroz speech-dispatcher, modul: %s\n", module);
  printf("=========================================================\n");
  int fails = 0;
  for(unsigned i=0;i<sizeof T/sizeof T[0];i++){
    spd_cancel_all(c); nap(300);
    pthread_mutex_lock(&mu); n_mark=0; n_end=0; got[0]=0; pthread_mutex_unlock(&mu);
    if(spd_say(c, SPD_MESSAGE, T[i].txt) < 0){ printf("  spd_say pao\n"); continue; }
    double dl = now_ms()+9000;
    while(now_ms()<dl){
      pthread_mutex_lock(&mu); int e=n_end; pthread_mutex_unlock(&mu);
      if(e>0) break;
      nap(20);
    }
    nap(200);
    pthread_mutex_lock(&mu); int m=n_mark; char g[256]; snprintf(g,sizeof g,"%s",got);
    pthread_mutex_unlock(&mu);
    const char *v = (m>=T[i].expect) ? "OK" : "<<< IZGUBLJEN";
    if(m<T[i].expect) fails++;
    printf("  %-52s markova=%d/%d [%s] %s\n", T[i].label, m, T[i].expect, g, v);
  }
  printf("\n%s\n", fails ? "SSIP putanja NASLEDJUJE problem." :
                           "SSIP putanja NE nasledjuje problem — svi markovi stigli.");
  spd_cancel_all(c);
  spd_close(c);
  return fails ? 1 : 0;
}
