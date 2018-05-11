#define _POSIX_C_SOURCE	199309L
#define _BSD_SOURCE

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <sys/time.h>
#include <time.h>

int ret = 0;
int end = 0;
int debug = 0;
int pipeA[2];
int pipeB[2];
pid_t pida, pidb;
ssize_t b;


void sig_handler(int signo) {
  if (signo == SIGINT)
    printf("Mottatt ctrl + c. Avslutter.\n");
    exit(3);
}

/* Denne funksjonen brukes for å kombinere en byte delt opp i fire, og sette det sammen til én.
 *
 * Input: char * tall som er delt i fire
 *
 * Return: int sammensatt tall
 */
int bytesToInt(char * input) {
  int aftern;
  aftern = ((unsigned char)input[0] << 24);
  aftern = aftern | ((unsigned char)input[1] << 16);
  aftern = aftern | ((unsigned char)input[2] << 8);
  aftern = aftern | (unsigned char)input[3];
  return aftern;
}

/* Main-funksjonen.
 * Kobler seg opp til en server og ber om jobber. Disse jobben blir så sendt videre til
 * to barneprosesser som printer dem ut.
 * Mainfunksjonen kan kjøres med det valgfrie argumentet '-d' for å aktivere debug-modus.
 */
int main(int argc, char *argv[]) {
  struct timespec tim;

  tim.tv_sec = 0;
  tim.tv_nsec = 50000000L;

  if (argc < 3 || argc > 4) {
    printf("Error: Trenger en ip-adresse og portnummer som argument\n");
    return 0;
  }
  if (argc == 4) {
    if (strcmp(argv[3], "-d") == 0) {
      printf("Debug-modus aktivert\n");
      debug = 1;
    }
  }

  /* Deklarerer datastruktur */
  struct sockaddr_in serveraddr;
  int sock;
  int buf;

  /* Oppretter socket */
  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock < 0) {
    perror("socket");
    exit(1);
  }
  if (debug == 1) printf(">>> %d <<< Oppretteter socket\n", getpid());

  /* Null ut serveradresse struct-en */
  memset(&serveraddr, 0, sizeof(serveraddr));

  /* Sett domenet til Internett */
  serveraddr.sin_family = AF_INET;
  if (debug == 1) printf(">>> %d <<< Setter domenet til internett\n", getpid());

  /* Sett inn internettadressen til localhost */
  serveraddr.sin_addr.s_addr = inet_addr(argv[1]);
  if (debug == 1) printf(">>> %d <<< Setter adressen til '%s'\n", getpid(), argv[1]);

  /* Sett portnummer */
  serveraddr.sin_port = htons(atoi(argv[2]));
  if (debug == 1) printf(">>> %d <<< Setter portnummer til %s\n", getpid(), argv[2]);

  /* Koble opp */
  ret = connect(sock, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
  if (ret == -1) {
    perror("connect");
    exit(1);
  }
  if (debug == 1) printf(">>> %d <<< Koblet seg opp til socket\n", getpid());

  // Lager pipes
  if (pipe(pipeA) < 0 || pipe(pipeB) < 0) {
    perror("pipe creation");
    exit(1);
  }
  if (debug == 1) printf(">>> %d <<< Opprettet pipes\n", getpid());

  if (debug == 1) printf(">>> %d <<< Kaller på fork() \n", getpid());
  pida = fork();

  if (pida == 0) {
    /* Barn O */
    for(;;) {
      /* Leser data om jobben fra forelder og printer dem ut med printf */
      unsigned char jobinfo;

      b = read(pipeA[0], &jobinfo, 1);
      if (b < 0) end = 1;
      if (debug == 1) printf(">>> %d <<< Leser jobbinfo fra forelder \n", getpid());
      printf("_______________________________________\n");

      int textlenght;

      b = read(pipeA[0], &textlenght, 4);
      if (b < 0) end = 1;
      if (debug == 1) printf(">>> %d <<< Leser tekstlengde fra forelder \n", getpid());

      char * textarr = malloc(textlenght);
      memset(textarr, 0, textlenght);
      b = read(pipeA[0], textarr, sizeof(char)*textlenght);
      if (b < 0) end = 1;
      if (debug == 1) printf(">>> %d <<< Leser inn tekst fra forelder \n", getpid());

      printf("Jobtype: O\n");
      printf("Tekstlengde: %d\n", textlenght);
      for (int i = 0; i < textlenght; i++) {
        printf("%c", textarr[i]);
      }
      printf("\n");
      free(textarr);
    }
  }
  else {
    if (debug == 1) printf(">>> %d <<< Kaller på fork() \n", getpid());
    pidb = fork();

    if (pidb == 0) {
      /* Barn E */
      for(;;) {
        /* Leser data om jobben fra forelder og printer dem ut med stderr */
        unsigned char jobinfo;

        b = read(pipeB[0], &jobinfo, 1);
        if (b < 0) end = 1;
        if (debug == 1) printf(">>> %d <<< Leser jobbinfo fra forelder \n", getpid());
        fprintf(stderr, "_______________________________________\n");

        int textlenght;

        b = read(pipeB[0], &textlenght, 4);
        if (b < 0) end = 1;
        if (debug == 1) printf(">>> %d <<< Leser tekstlengde fra forelder \n", getpid());

        char * textarr = malloc(textlenght);
        b = read(pipeB[0], textarr, sizeof(char)*textlenght);
        if (b < 0) end = 1;
        if (debug == 1) printf(">>> %d <<< Leser inn tekst fra forelder \n", getpid());

        fprintf(stderr, "Jobtype: E\n");
        fprintf(stderr, "Tekstlengde: %d\n", textlenght);
        for (int i = 0; i < textlenght; i++) {
          fprintf(stderr, "%c", textarr[i]);
        }
        fprintf(stderr, "\n");
        free(textarr);
      }
    }
    else {
      /*  Forelder
       *  Lager en meny og sender valg til server.
       *  Mottar jobdata og sender dette videre til riktig barn.
       */

      if (signal(SIGINT, sig_handler) == SIG_ERR) {}
      for(;;) {
        char reply_buff[255] = { '\0' };
        if (end == 1) {
          reply_buff[0] = '4';
        }
        else {
          printf("_______________________________________\n");
          printf("1. Hent én jobb\n2. Hent valgfritt antall jobber\n3. Hent alle resterende jobber\n4. Avslutt\n");
          fgets(reply_buff, sizeof(reply_buff), stdin);
        }

        // 1. Hent en jobb
        if (reply_buff[0] == '1') {
          buf = 1;

          if (debug == 1) printf(">>> %d <<< Skriver brukervalg til server \n", getpid());
          b = write(sock, &buf, 4);
          if (b < 0) end = 1;

          unsigned char jobinfo;
          b = read(sock, &jobinfo, 1);
          if (b < 0) end = 1;
          if (debug == 1) printf(">>> %d <<< Leser jobinfo fra server \n", getpid());

          // check if 'Q' later
          if (jobinfo == 224) {
            printf("Mottokk Q melding, avslutter\n");
            end = 1;
          }
          else {
            if ((jobinfo >> 5)  == 0){
              if (debug == 1) printf(">>> %d <<< Skriver jobinfo til barn \n", getpid());
              b = write(pipeA[1], &jobinfo, 1);
              if (b < 0) end = 1;

            }
            else if ((jobinfo >> 5) == 1) {
              if (debug == 1) printf(">>> %d <<< Skriver jobinfo til barn \n", getpid());
              b = write(pipeB[1], &jobinfo, 1);
              if (b < 0) end = 1;
            }
            nanosleep(&tim, NULL);

            char * lengtharr = malloc(4);
            memset(lengtharr, 0, 4);

            b = read(sock, lengtharr, 4);
            if (b < 0) end = 1;
            if (debug == 1) printf(">>> %d <<< Leser tekstlengde fra server \n", getpid());

            int textlenght = bytesToInt(lengtharr);
            free(lengtharr);

            if ((jobinfo >> 5)  == 0) {
              if (debug == 1) printf(">>> %d <<< Skriver tekstlengde til barn \n", getpid());
              b = write(pipeA[1], &textlenght, 4);
              if (b < 0) end = 1;
            }
            else if ((jobinfo >> 5) == 1) {
              if (debug == 1) printf(">>> %d <<< Skriver tekstlengde til barn \n", getpid());
              b = write(pipeB[1], &textlenght, 4);
              if (b < 0) end = 1;
            }
            nanosleep(&tim, NULL);

            char * textarr = malloc(textlenght);
            b = read(sock, textarr, sizeof(char) * textlenght);
            if (b < 0) end = 1;
            if (debug == 1) printf(">>> %d <<< Leser tekst fra server \n", getpid());

            int textSum = 0;
            for (int i = 0; i < textlenght; i++) {
              textSum += (int) textarr[i];
            }
            unsigned char checksum = textSum % 32;

            unsigned char rc = jobinfo & 31;
            if (rc != checksum) {
              printf("Checksummer er ikke like: %d != %d\n", rc, checksum);
              printf("Ignorerer jobb\n");
            }

            else {
              if ((jobinfo >> 5) == 0) {
                if (debug == 1) printf(">>> %d <<< Skriver tekst til barn \n", getpid());
                b = write(pipeA[1], textarr, sizeof(char)*textlenght);
                if (b < 0) end = 1;
              }
              else if ((jobinfo >> 5) == 1) {
                if (debug == 1) printf(">>> %d <<< Skriver tekst til barn \n", getpid());
                b = write(pipeB[1], textarr, sizeof(char)*textlenght);
                if (b < 0) end = 1;
              }
              nanosleep(&tim, NULL);
              if (textlenght > 1000) {
                sleep(1);
              }
            }
            free(textarr);
          }
        }

        // 2. Hent x jobber
        else if (reply_buff[0] == '2') {
          buf = 2;
          if (debug == 1) printf(">>> %d <<< Skriver valg til server \n", getpid());
          b = write(sock, &buf, 4);
          if (b < 0) end = 1;
          printf("Hvor mange jobber vil du hente?\n");
          char * amount = malloc(10);
          memset(amount, 0, 10);
          fgets(amount, 10, stdin);
          buf = atoi(amount);
          while (buf < 0) {
            printf("Error: Skriv inn et positivt heltall\n");
            printf("Hvor mange jobber vil du hente?\n");
            memset(amount, 0, 10);
            fgets(amount, 10, stdin);
            buf = atoi(amount);
          }
          if (debug == 1) printf(">>> %d <<< Skriver antall jobber som skal hentes til server \n", getpid());
          b = write(sock, &buf, 4);
          if (b < 0) end = 1;
          free(amount);
          for (int i = 0; i < buf; i++) {
            unsigned char jobinfo;
            b = read(sock, &jobinfo, 1);
            if (b < 0) end = 1;
            if (debug == 1) printf(">>> %d <<< Leser jobinfo fra server \n", getpid());

            if (jobinfo == 224) {
              printf("Mottokk Q melding, avslutter\n");
              end = 1;
              break;
            }
            else {
              if ((jobinfo >> 5)  == 0) {
                if (debug == 1) printf(">>> %d <<< Skriver jobinfo til barn \n", getpid());
                b = write(pipeA[1], &jobinfo, 1);
                if (b < 0) end = 1;
              }
              else if ((jobinfo >> 5) == 1) {
                if (debug == 1) printf(">>> %d <<< Skriver jobinfo til barn \n", getpid());
                b = write(pipeB[1], &jobinfo, 1);
                if (b < 0) end = 1;
              }
              nanosleep(&tim, NULL);

              char * lengtharr = malloc(4);
              memset(lengtharr, 0, 4);
              if (debug == 1) printf(">>> %d <<< Leser tekstlengde fra server \n", getpid());
              b = read(sock, lengtharr, 4);
              if (b < 0) end = 1;

              int textlenght = bytesToInt(lengtharr);
              free(lengtharr);

              if ((jobinfo >> 5)  == 0) {
                if (debug == 1) printf(">>> %d <<< Skriver tekstlengde til barn \n", getpid());
                b = write(pipeA[1], &textlenght, 4);
                if (b < 0) end = 1;
              }
              else if ((jobinfo >> 5) == 1) {
                if (debug == 1) printf(">>> %d <<< Skriver tekstlengde til barn \n", getpid());
                b = write(pipeB[1], &textlenght, 4);
                if (b < 0) end = 1;
              }
              nanosleep(&tim, NULL);

              char * textarr = malloc(textlenght);
              b = read(sock, textarr, sizeof(char)*textlenght);
              if (b < 0) end = 1;
              if (debug == 1) printf(">>> %d <<< Leser tekst fra server \n", getpid());

              int textSum = 0;
              for (int i = 0; i < textlenght; i++) {
                textSum += (int) textarr[i];
              }
              unsigned char checksum = textSum % 32;
              unsigned char rc = jobinfo & 31;
              if (rc != checksum) {
                printf("Checksummer er ikke like: %d != %d\n", rc, checksum);
                printf("Ignorerer jobb\n");
              }

              else {
                if ((jobinfo >> 5)  == 0) {
                  if (debug == 1) printf(">>> %d <<< Skriver tekst til barn \n", getpid());
                  b = write(pipeA[1], textarr, sizeof(char)*textlenght);
                  if (b < 0) end = 1;
                }
                else if ((jobinfo >> 5) == 1) {
                  if (debug == 1) printf(">>> %d <<< Skriver tekst til barn \n", getpid());
                  b = write(pipeB[1], textarr, sizeof(char)*textlenght);
                  if (b < 0) end = 1;
                }
                nanosleep(&tim, NULL);
                if (textlenght > 1000) {
                  sleep(1);
                }
              }
              free(textarr);
            }
          }
        }

        // 3. Hent alle jobber
        else if (reply_buff[0] == '3') {
          buf = 3;
          if (debug == 1) printf(">>> %d <<< Skriver valg til server \n", getpid());
          b = write(sock, &buf, 4);
          if (b < 0) end = 1;
          for (;;) {
            unsigned char jobinfo;
            read(sock, &jobinfo, 1);
            if (debug == 1) printf(">>> %d <<< Leser jobinfo fra server \n", getpid());

            if (jobinfo == 224) {
              printf("Mottokk Q melding, avslutter\n");
              end = 1;
              break;
            }
            else {
              if ((jobinfo >> 5)  == 0) {
                if (debug == 1) printf(">>> %d <<< Skriver jobinfo til barn \n", getpid());
                b = write(pipeA[1], &jobinfo, 1);
                if (b < 0) end = 1;
              }
              else if ((jobinfo >> 5) == 1) {
                if (debug == 1) printf(">>> %d <<< Skriver jobinfo til barn \n", getpid());
                b = write(pipeB[1], &jobinfo, 1);
                if (b < 0) end = 1;
              }
              nanosleep(&tim, NULL);

              char * lengtharr = malloc(4);
              memset(lengtharr, 0, 4);
              b = read(sock, lengtharr, 4);
              if (debug == 1) printf(">>> %d <<< Leser tekstlengde fra server \n", getpid());
              if (b < 0) end = 1;
              int textlenght = bytesToInt(lengtharr);
              free(lengtharr);

              if ((jobinfo >> 5)  == 0) {
                if (debug == 1) printf(">>> %d <<< Skriver tekstlengde til barn \n", getpid());
                b = write(pipeA[1], &textlenght, 4);
                if (b < 0) end = 1;
              }
              else if ((jobinfo >> 5) == 1) {
                if (debug == 1) printf(">>> %d <<< Skriver tekstlengde til barn \n", getpid());
                b = write(pipeB[1], &textlenght, 4);
                if (b < 0) end = 1;
              }
              nanosleep(&tim, NULL);

              char * textarr = malloc(textlenght);
              b = read(sock, textarr, sizeof(char)*textlenght);
              if (debug == 1) printf(">>> %d <<< Leser tekstlengde fra server \n", getpid());
              if (b < 0) end = 1;

              int textSum = 0;
              for (int i = 0; i < textlenght; i++) {
                textSum += (int) textarr[i];
              }
              unsigned char checksum = textSum % 32;

              unsigned char rc = jobinfo & 31;
              if (rc != checksum) {
                printf("Checksummer er ikke like: %d != %d\n", rc, checksum);
                printf("Ignorer jobb\n");
              }

              else {
                if ((jobinfo >> 5)  == 0) {
                  if (debug == 1) printf(">>> %d <<< Skriver tekstlengde til barn \n", getpid());
                  b = write(pipeA[1], textarr, sizeof(char)*textlenght);
                  if (b < 0) end = 1;
                }
                else if ((jobinfo >> 5) == 1) {
                  if (debug == 1) printf(">>> %d <<< Skriver tekstlengde til barn \n", getpid());
                  b = write(pipeB[1], textarr, sizeof(char)*textlenght);
                  if (b < 0) end = 1;
                }
                nanosleep(&tim, NULL);
                if (textlenght > 1000) {
                  sleep(1);
                }
              }
              free(textarr);
            }
          }
        }

        // 4. Avslutt
        else if (reply_buff[0] == '4') {
          if (debug == 1) printf(">>> %d <<< Avslutter \n", getpid());
          printf("Avslutter normalt\n");
          buf = 4;
          b = write(sock, &buf, 4);
          unsigned char clear = 'c';
          close(pipeA[0]);
          close(pipeA[1]);
          close(pipeB[0]);
          close(pipeB[1]);
          if (debug == 1) printf(">>> %d <<< Lukker pipes \n", getpid());
          kill(pida, SIGTERM);
          kill(pidb, SIGTERM);
          if (debug == 1) printf(">>> %d <<< Terminerer barn \n", getpid());
          if (debug == 1) printf(">>> %d <<< Skriver klarmelding til server \n", getpid());
          write(sock, &clear, 1);
          break;
        }
        else {
          printf("Error: Skriv inn et tall!\n");
        }
      }
    }
  }

  /* Stenger socketen */
  if (debug == 1) printf(">>> %d <<< Stenger socket \n", getpid());
  close(sock);

  /* Program kjørte ordentlig - returner suksessverdi */
  return EXIT_SUCCESS;
}
