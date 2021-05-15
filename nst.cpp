#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <cstdint>
#include <time.h>
#include <Windows.h>

#define ASCII_XON       0x11
#define ASCII_XOFF      0x13

void showTime () {
    auto timestamp = time (0);
    auto now = localtime (& timestamp);

    printf ("[%02d:%0d:%02d] ", now->tm_hour, now->tm_min, now->tm_sec);
}

HANDLE openPort (int portNo, int baud) {
    char portName [50];
    sprintf (portName, "\\\\.\\COM%d", portNo);

    HANDLE port = CreateFile (portName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_ALWAYS, 0, 0);

    if (port != INVALID_HANDLE_VALUE) {
        COMMTIMEOUTS timeouts;
        DCB state;

        memset (& state, 0, sizeof (state));

        state.DCBlength = sizeof (state);

        SetupComm (port, 4096, 4096); 
        PurgeComm (port, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR); 
        GetCommState (port, & state);
        GetCommTimeouts (port, & timeouts);

        timeouts.ReadIntervalTimeout = 1000;
        timeouts.ReadTotalTimeoutMultiplier = 1;
        timeouts.ReadTotalTimeoutConstant = 3000;

        state.fInX = state.fOutX = state.fParity = state.fBinary = 1;
        state.BaudRate = baud;
        state.ByteSize = 8;
        state.XoffChar = ASCII_XOFF;
        state.XonChar = ASCII_XON;
        state.XonLim = state.XoffLim = 100;
        state.Parity = PARITY_NONE;
        state.StopBits = ONESTOPBIT;

        SetCommTimeouts (port, & timeouts);
        SetCommState (port, & state);
    }

    return port;
}

void readFeedbackData (HANDLE port) {
    COMSTAT commState;
    unsigned long errorFlags, bytesRead, errorCode;
    bool overflow;
    char buffer [5000];

    while (true) {
        ClearCommError (port, & errorFlags, & commState);

        overflow = (errorFlags & (CE_RXOVER | CE_OVERRUN)) != 0L;

        if (commState.cbInQue > 0) {
            if (ReadFile (port, buffer, commState.cbInQue, & bytesRead, 0)) {
                if (bytesRead > 0) {
                    buffer [bytesRead] = '\0';

                    showTime (); printf (buffer);
                }
            } else {
                showTime (); printf ("Error %d reading port\n", GetLastError ());
            }
        }
        
        Sleep (10);
    }
}

void showError (char *msg) {
    showTime ();
    printf ("ERROR: %s\n", msg);
    exit (0);
}

void sendSentence (char *sentence, HANDLE port) {
    if (sentence [0] != '$' && sentence [0] != '!') {
        showError ("NMEA sentence must start from $ or ! character");
    }

    char *asteriskPos = strchr (sentence, '*');

    if (asteriskPos == 0) {
        showError ("NMEA sentence must be finished by asterisk character followed by two hex digit CRC");
    }

    uint8_t crc = sentence [1];

    for (auto chr = sentence + 2; chr < asteriskPos; ++ chr) {
        crc ^= *chr;
    }

    sprintf (asteriskPos + 1, "%02X\r\n", crc);

    unsigned long bytesWritten;
    WriteFile (port, sentence, (unsigned long) strlen (sentence), & bytesWritten, 0);
    printf (sentence);
}

void showHelpAndExit () {
    printf (
        "Usage:\n\n"
        "\tnst options\n\n"
        "\twhere options are:\n"
        "\t\t-p:n\t\t\t\tselects the serial port COMn\n"
        "\t\t-b:n\t\t\t\tselects the baud rate\n"
        "\t\t-s:sentence like $TIABC,...*00h\t(<cr><lf> will be added automatically, CRC after an askerisk will be calculated)\n"
        "\t\t-r\t\t\t\tenables feedback reading\n"
        "\t\t-h\t\t\t\tshows the help\n"
    );
    exit (0);
}

int main (int argCount, char *args []) {
    char sentence [100] { "" };
    int port = 1;
    int baud = 4800;
    bool readFeedback = false;

    printf ("NMEA sentence tool\nCopyright (c) by Evgeny Tukh, 2021\n\n");

    // parse args
    auto showInvalidArg = [] (char *arg) {
        printf ("Invalid argument %s\n", arg);
        exit (0);
    };

    if (argCount < 2) showHelpAndExit ();

    for (int i = 1; i < argCount; ++ i) {
        auto arg = args [i];

        if (*arg == '-' || *arg == '/') {
            switch (toupper (arg [1])) {
                case 'H': {
                    showHelpAndExit ();
                }
                case 'S': {
                    if (arg [2] == ':') {
                        strcpy (sentence, arg + 3); break;
                    } else {
                        showInvalidArg (arg);
                    }
                }
                case 'P': {
                    if (arg [2] == ':') {
                        port = atoi (arg + 3); break;
                    } else {
                        showInvalidArg (arg);
                    }
                }
                case 'B': {
                    if (arg [2] == ':') {
                        baud = atoi (arg + 3); break;
                    } else {
                        showInvalidArg (arg);
                    }
                }
                case 'R': {
                    readFeedback = true; break;
                }
            }
        } else {
            showInvalidArg (arg);
        }
    }

    HANDLE portHandle = openPort (port, baud);

    if (portHandle == INVALID_HANDLE_VALUE) {
        showTime ();
        printf ("Unable to open COM%d, error %d\n", port, GetLastError ());
    } else {
        showTime ();
        printf ("Sending to COM%d (%d): ", port, baud);
        if (*sentence) {
            sendSentence (sentence, portHandle);
        }

        showTime ();
        printf ("Waiting for response...\n");
        if (readFeedback) {
            readFeedbackData (portHandle);
        }

        CloseHandle (portHandle);
   }

    exit (0);
}