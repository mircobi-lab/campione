#pragma once
#include <juce_core/juce_core.h>

/*  Dai file Yamaha YSFC a SoundFont 2 in memoria: Campione suona le waveform utente di Motif e Montage
    con lo stesso motore dei file .sf2, e le può salvare come .sf2.

    File letti (struttura YSFC, uguale in tutti, cambiano versione e dettagli):
      Motif XS .X0A .X0W   Motif XF .X3A .X3W   MOXF .X6A .X6W
      Montage .X7U .X7L .X7A   MODX / MODX+ .X8U .X8L .X8A   Montage M / MODX M .Y2U .Y2L

    Ogni waveform utente diventa un preset (banco 0, programmi 0-127, poi banco 1...) con il nome della waveform.
    Ogni keybank diventa una zona: tasti, velocity, nota base, intonazione, livello, pan, inizio, loop.
    Non vengono convertiti, perché non stanno nel file o non sono campioni:
      - le waveform di fabbrica (ROM): le Performance che le usano non hanno campioni nel file
      - le parti FM-X
      - i parametri delle Performance (filtri, inviluppi, effetti, Motion Control)
    Vengono saltate e contate: waveform cifrate (contenuti protetti) e compresse (WXC), loop all'indietro
    (diventano loop in avanti).

    Riferimenti per il formato: la documentazione di Chris Webb (github.com/arachsys/montage, file
    docs/files/structure.md) e il lettore YSFC di ConvertWithMoss di Jürgen Moßgraber
    (github.com/git-moss/ConvertWithMoss, LGPL v3), da cui vengono la disposizione dei keybank
    per modello e le conversioni di livello e intonazione. Il codice qui è scritto da zero.              */
namespace campione::ysfc
{
struct Result
{
    juce::MemoryBlock sf2;
    juce::String error;              // vuoto se è andato tutto bene
    juce::String model;              // "MONTAGE", "MOTIF XF"...
    int waveforms = 0, keybanks = 0, skipped = 0;
    juce::StringArray notes;         // avvisi da mostrare
};

bool isYamahaFile (const juce::File&);
juce::String wildcard();             // "*.x0a;*.x0w;..." per il selettore dei file
Result convert (const juce::File&);
}
