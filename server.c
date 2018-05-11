#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <signal.h>

FILE *file;
int filesize;
int fLoactionCount = 0;
int ret = 0;
int textlengthGlobal = 0;
int end = 0;
int debug = 0;
ssize_t bytesRead;
ssize_t bytesWritten;

/* Denne funksjonen tar inn et signal, lukker filen og avlutter programmet.
 *
 * Input: int signal
 */
void sig_handler(int signo) {
  if (signo == SIGINT)
    printf("Mottatt ctrl + c. Avslutter.\n");
    fclose(file);
    exit(3);
}

/* Denne funksjonen leser en job fra jobfilen og returnerer en char peker
 * som inneholder meldingen.
 * Funksjonen oppdaterer fLocationCount med hvor den er i filen.
 */
char * createMessage() {
  unsigned char jobinfo;
  int l;
  char jobchar[1];
  if (fLoactionCount == filesize) {
    if (debug == 1) printf(">>> %d <<< Er på slutten av filen\n", getpid());
  }

  if (fLoactionCount == filesize) {
    jobinfo = 7 << 5;
    char * messageToClient = malloc(sizeof(unsigned char) * 5);
    memset(messageToClient, 0, sizeof(unsigned char) * 5);
    messageToClient[0] = jobinfo;
    textlengthGlobal = 0;
    return messageToClient;
  }

  if (debug == 1) printf(">>> %d <<< Leser jobchar fra fil\n", getpid());
  fread(jobchar, 1, 1, file);
  fLoactionCount++;
  if (jobchar[0] == 'E') {
    jobinfo = 1 << 5;
  }
  else if (jobchar[0] == 'O') {
    jobinfo = 0;
  }

  unsigned int textlength [1];
  if (debug == 1) printf(">>> %d <<< Leser tekstlengde fra fil\n", getpid());
  fread(textlength, 4, 1, file);
  fLoactionCount += 4;
  l = (int) textlength[0];
  textlengthGlobal = l;

  char * messageToClient = malloc(sizeof(unsigned char) * (l + 5));
  memset(messageToClient, 0, sizeof(unsigned char) * (l + 5));
  char text[l];
  int lengthRead = sizeof(char) * l;
  if (debug == 1) printf(">>> %d <<< Leser teksten fra fil\n", getpid());
  fread(text, sizeof(char) * l, 1, file);
  fLoactionCount += lengthRead;

  if (debug == 1) printf(">>> %d <<< Kalkulerer checksum\n", getpid());
  int textSum = 0;
  for (int i = 0; i < l; i++) {
    textSum += (int) text[i];
  }
  int checksum = textSum % 32;

  jobinfo = jobinfo | checksum;
  messageToClient[0] = jobinfo;

  if (debug == 1) printf(">>> %d <<< Fordeler tekstlengde int på fire plasser\n", getpid());
  messageToClient[1] = (unsigned char)(l >> 24);
  messageToClient[2] = (unsigned char)(l >> 16);
  messageToClient[3] = (unsigned char)(l >> 8);
  messageToClient[4] = (unsigned char) l;

  if (debug == 1) printf(">>> %d <<< Skriver inn tekst i meldningen\n", getpid());
  for (int i = 0; i < l; i++) {
    messageToClient[i+5] = text[i];
  }

  return messageToClient;
}

/* Denne funksjonen sender en melding, laget av createMessage(), til klienten.
 * Når den kommer til slutten av jobfilen sender den en 'Q' melding.
 *
 * Input: int antall meldinger, int socketnr.
 */
void sendMessage(int amount, int socket) {

  for (int i = 0; i < amount; i++) {
    if (debug == 1) printf(">>> %d <<< Lager melding\n", getpid());
    char * message = createMessage();

    if ((unsigned char) message[0] == 224) {
      if (debug == 1) printf(">>> %d <<< Skriver siste melding til klient\n", getpid());
      bytesWritten = write(socket, message, textlengthGlobal + 5);
      if (bytesWritten != (textlengthGlobal + 5)) {
        perror("bytesWritten");
        end = 1;
      }
      free(message);
      end = 1;
      break;
    }

    if (debug == 1) printf(">>> %d <<< Skriver melding til klient\n", getpid());
    bytesWritten = write(socket, message, textlengthGlobal + 5);
    if (bytesWritten != (textlengthGlobal + 5)) {
      perror("bytesWritten");
    }
    free(message);
  }
}


/* Main-funksjonen.
 * Tar inn filnavn og portnummer som argument. Kan kjøres i debug-modes
 * ved å legge til en '-d' som siste argument.
 * Oppretter en request socket som stenges etter at den har blitt koblet opp mot en
 * klient slik at ingen andre klienter kan koble seg på.
 * Tar inn valg fra klienten, lager meldinger og sender til klienten.
 */
int main (int argc, char *argv[]) {
  if (signal(SIGINT, sig_handler) == SIG_ERR)
  if (argc < 3 || argc > 4) {
    printf("Error: Needs a filename and port as argument\n");
    return 0;
  }
  if (argc == 4) {
    if (strcmp(argv[3], "-d") == 0) {
      printf("Debug-modus aktivert\n");
      debug = 1;
    }
  }

  file = fopen(argv[1], "r");
  if (file == NULL) {
    printf("Error: File is null\n");
    return 0;
  }
  else printf("Success: File '%s' opened successfully\n", argv[1]);
  fseek(file, 0, SEEK_END);
  filesize = ftell(file);
  fseek(file, 0, SEEK_SET);
  printf("Size of file is: %d\n", filesize);

  /* Server variabler */
  struct sockaddr_in serveraddr, clientaddr;
  socklen_t clientaddrlen = 0;
  memset(&clientaddr, 0, sizeof(clientaddr));
  int request_sock, sock;
  int buf;
  if (debug == 1) printf(">>> %d <<< Initialiserer server variabler\n", getpid());

  /* Opprett request-socket  */
  request_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (request_sock == -1) {
    perror("socket");
    exit(1);
  }
  if (debug == 1) printf(">>> %d <<< Oppretteter socket\n", getpid());

  /* Opprett adressestruct */
  memset(&serveraddr, 0, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = INADDR_ANY;
  serveraddr.sin_port = htons(atoi(argv[2]));
  if (debug == 1) printf(">>> %d <<< Oppretteter serveradresse\n", getpid());

  /* Bind adressen til socketen */
  ret = bind(request_sock, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
  if (ret == -1) {
    perror("bind");
    exit(1);
  }
  if (debug == 1) printf(">>> %d <<< Binder adressen til socketen\n", getpid());

  /* Aktiver lytting på socketen */
  printf("Listening on port: %s\n", argv[2]);
  ret = listen(request_sock, SOMAXCONN);
  if (ret == -1) {
    perror("listen");
    exit(1);
  }
  if (debug == 1) printf(">>> %d <<< Aktiverer lytting på socketen\n", getpid());

  /* Motta en forbindelse */
  sock = accept(request_sock, (struct sockaddr *)&clientaddr, &clientaddrlen);
  if (sock == -1) {
    perror("accept");
    exit(1);
  }
  if (debug == 1) printf(">>> %d <<< Aksepterer forbindelse \n", getpid());

  /* Lukker request socket */
  ret = close(request_sock);
  if (ret == -1) {
    perror("close request socket");
    exit(1);
  }
  if (debug == 1) printf(">>> %d <<< Lukker request socket\n", getpid());

  /* Les data fra forbindelsen, og skriv dem ut */
  for(;;) {
    if (end == 1) {
      buf = 4;
    }
    else {
      bytesRead = read(sock, &buf, 4);
      if (debug == 1) printf(">>> %d <<< Leser valg fra client\n", getpid());
      if (bytesRead != 4) {
        perror("bytesRead");
        end = 1;
        buf = 4;
      }
    }
    if (debug == 1) printf(">>> %d <<< Fikk valg: %d\n", getpid(), buf);

    // 1. Send en melding
    if (buf == 1) {
      sendMessage(1, sock);
    }

    // 2. Send x meldinger
    else if (buf == 2) {
      bytesRead = read(sock, &buf, 4);
      if (debug == 1) printf(">>> %d <<< Leser antall fra klient\n", getpid());
      if (bytesRead < 0) {
        end = 1;
      }
      int n = buf;
      sendMessage(n, sock);
    }

    // 3. Send alle resterende meldinger
    else if (buf == 3) {
      if (debug == 1) printf(">>> %d <<< Sender resterende meldinger til klient\n", getpid());
      sendMessage(2147483646, sock);
    }

    // 4. Avslutter
    else if (buf == 4) {
      unsigned char clear = 0;
      printf("Venter på avsluttningmelding fra client...\n");
      bytesRead = read(sock, &clear, 1);
      if (bytesRead < 0) {
        end = 1;
      }
      if (debug == 1) printf(">>> %d <<< Leser klarmelding fra klient\n", getpid());
      if (clear == 'c') {
        printf("Success: Mottat avsluttingsmelding\n");
        if (debug == 1) printf(">>> %d <<< Stenger socket\n", getpid());

        /* Steng socketene */
        ret = close(sock);
        if (ret == -1) {
          perror("close socket");
          exit(1);
        }
        if (debug == 1) printf(">>> %d <<< Lukker fil\n", getpid());
        fclose(file);
        break;
      }
      else if (end == 1) {

        /* Steng socketene */
        printf("Error: Uventet avsluttning\n");
        if (debug == 1) printf(">>> %d <<< Stenger socket\n", getpid());
        ret = close(sock);
        if (ret == -1) {
          perror("close socket");
          exit(1);
        }
        if (debug == 1) printf(">>> %d <<< Lukker fil\n", getpid());
        fclose(file);
        break;
      }

    }
  }
  return 0;
}
