#pragma once
// Accesso alle regioni di TinySoundFont per sostituire l'inviluppo d'ampiezza (TsfAccess.cpp contiene anche l'implementazione di tsf.h).
struct tsf;
namespace campione::tsfaccess
{
constexpr int envFloats = 8;                      // delay, attack, hold, decay, sustain, release, keynumToHold, keynumToDecay
int regionCount (const tsf*);
void presetNumbers (const tsf*, int presetIndex, int& bank, int& number);
void readEnvelopes (const tsf*, float* out);      // out: regionCount * envFloats
void writeEnvelopes (tsf*, const float* in);
// tempi in secondi, sustain come guadagno 0..1 (stessa unità che TinySoundFont usa dopo il caricamento)
int voicesOnKey (const tsf*, int key);          // per le prove
// Prima di una nota nuova: chiude con una dissolvenza le voci ancora attive sullo stesso tasto (come la corda colpita di nuovo)
// e, se restano meno di minFree voci libere, le voci più vecchie. Così TinySoundFont non deve mai troncarne una di colpo.
void prepareNoteOn (tsf*, int channel, int key, int minFree, float restrikeFade, float stealFade);
void overrideEnvelopes (tsf*, float attack, float decay, float sustain, float release);
// Mezzo pedale: le voci tenute dal pedale (tasto già lasciato) partono in rilascio con il tempo indicato.
void dampHeld (tsf*, float seconds);
}
