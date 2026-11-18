[English](General.md) · **Deutsch**

[Startseite](DE-Home.md) · [Menü bedienen](DE-Using-the-menu.md) · [Quellen](DE-Sources.md)

Ausgabe, Bildanalyse, Hintergrund, Anzeigemodus und Darstellung des Menüs.

**Auf dieser Seite**

- [Typische Situationen](#general-examples)
- [Ausgabe](#general-output)
- [Bildanalyse](#general-image-analysis)
- [Hintergrund und Transparenz](#general-background-transparency)
- [Anzeigemodus](#general-display-mode)
- [Diagnose](#general-diagnostics)
- [Darstellung](#general-themes)

<a id="general-examples"></a>

## Typische Situationen

Beispiele aus dem Alltag. Ändere jeweils nur eine Einstellung und vergleiche dieselbe Szene.

| Situation | Einstellung | Worauf achten? |
| --- | --- | --- |
| Am Fernseher wirkt Schwarz grau und das ganze Bild flau. | [Ausgabebereich für RGB](#general-output-rgb-output-levels) auf den anderen Wert stellen, etwa **Begrenzt (TV, 16-235)** statt **Voll (PC, 0-255)**. | Nachthimmel und dunkle Kleidung sollen satt schwarz sein und trotzdem Details zeigen. |
| Schatten versinken komplett, helle Wolken wirken ausgefressen. | Ebenfalls [Ausgabebereich für RGB](#general-output-rgb-output-levels) prüfen. | Bildschirm, Grafiktreiber und FrankyRenderer müssen denselben Bereich verwenden. |
| Kinofilme ruckeln bei Kameraschwenks am 60-Hz-Fernseher. | [Anzeigemodus im Vollbild wechseln](#general-display-mode-switch-display-mode-in-fullscreen) **Ein**, [Zulässige Modi](#general-display-mode-allowed-modes) `23, 24, 50, 59, 60`. | Strg+J zeigt die neue Bildwiederholrate. Ein 23,976-fps-Film läuft dann mit 23,976 Hz. |
| Nach dem Umschalten fehlen die ersten Sekunden, weil der Fernseher noch schwarz ist. | [Pause nach dem Wechsel](#general-display-mode-pause-after-switching-seconds) auf 3 bis 5 Sekunden. | Die Wiedergabe soll erst starten, wenn das Bild wieder da ist. |
| Vom Sofa aus ist die Strg+J-Statistik zu klein. | [Textgröße der Statistik](#general-image-analysis-statistics-text-size) auf 150. | 100 ist die automatische Größe. |
| Du willst sehen, ob Lichter oder Schatten abgeschnitten werden. | [Bildanalyse](#general-image-analysis-image-analysis) **Ein**, etwa mit Histogramm oder Falschfarben. | Für normales Schauen wieder ausschalten. |
| Ein Fehler tritt auf und du willst ihn melden. | [Diagnoseprotokoll](#general-diagnostics-diagnostic-log) **Ein**, Player neu starten, Fehler nachstellen, dann [Bericht speichern](#general-diagnostics-save-report). | Der Bericht liegt danach auf dem Desktop. |

<a id="general-output"></a>

## Ausgabe

Prüfe RGB-Ausgabepegel und Eingangsbereich am Display gemeinsam. Bei einem Pegelkonflikt können Schwarz und Weiß abgeschnitten oder ausgewaschen wirken.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="general-output-output-texture-format"></a>**Ausgabe-Texturformat** | Bestimmt, wie fein Helligkeiten und Farben im Ausgabebild abgestuft sind. Eine höhere Präzision kann sichtbare Stufen in weichen Verläufen reduzieren, wenn der Ausgabepfad sie unterstützt. |
| <a id="general-output-rgb-output-levels"></a>**Ausgabebereich für RGB** | Stimmt den Schwarz- und Weißpegel auf Bildschirm und Treiber ab. Ein falscher Bereich macht Schwarz grau oder verschluckt Details in Schatten und Lichtern. |
| <a id="general-output-dither-target-bit-depth"></a>**Ziel-Bittiefe für Dithering** | Passt das Dithering an die Bittiefe der Ausgabe an. Automatisch folgt dem Ausgabeformat. Eine passende Bittiefe kaschiert Stufen in Farbverläufen. |
| <a id="general-output-max-frames-in-flight"></a>**Maximal gleichzeitig verarbeitete Bilder** | Erlaubt der GPU, mehrere Bilder parallel vorzubereiten. Ein größerer Puffer kann Lastspitzen abfangen, erhöht aber die Latenz. 0 lässt das Gerät wählen. |

<a id="general-image-analysis"></a>

## Bildanalyse

Die Strg+J-Statistik und die Bildanalyse erscheinen direkt im Video.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="general-image-analysis-image-analysis"></a>**Bildanalyse** | Bei **Ein** zeigt die Bildanalyse Messbilder wie Wellenform, Histogramm oder Falschfarben über dem Video. **Aus** blendet diese Anzeige aus. |
| <a id="general-image-analysis-statistics-text-size"></a><a id="general-image-analysis-statistics-in-video-area"></a>**Textgröße der Statistik (%)** | Größe der Ctrl+J-Statistik. 100 ist die automatische Größe. |

<a id="general-background-transparency"></a>

## Hintergrund und Transparenz

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="general-background-transparency-background-red"></a>**Hintergrund: Rot** | Rotanteil für unbedeckte Ränder und transparente Pixel. |
| <a id="general-background-transparency-background-green"></a>**Hintergrund: Grün** | Grünanteil für unbedeckte Ränder und transparente Pixel. |
| <a id="general-background-transparency-background-blue"></a>**Hintergrund: Blau** | Blauanteil für unbedeckte Ränder und transparente Pixel. |
| <a id="general-background-transparency-background-transparency"></a>**Hintergrundtransparenz** | 0 ist undurchsichtig, 1 ist transparent. Ändert den gerenderten Alphawert, nicht die Transparenz des Desktops oder Fensters in Windows. |
| <a id="general-background-transparency-corner-rounding"></a>**Eckenrundung** | Relative Rundung der Videoecken. 0 behält rechteckige Ecken bei, 1 wendet die maximale Rundung an. |
| <a id="general-background-transparency-skip-target-clearing"></a>**Löschen der Ausgabefläche überspringen** | Bei **Aus (Standard)** wird der Hintergrund um das Video neu gezeichnet. Bei **Ein** können alte Bildreste in den Rändern stehen bleiben. |
| <a id="general-background-transparency-blend-against-tiles"></a>**Schachbretthintergrund verwenden** | Bei **Ein** siehst du in transparenten Bildbereichen ein Schachbrett. Bei **Aus (Standard)** erscheint dort der normale Hintergrund. |
| <a id="general-background-transparency-light-tile-red"></a>**Helle Felder: Rot** | Rotanteil der hellen Schachbrettfelder. Wird mit dem Schachbretthintergrund verwendet. |
| <a id="general-background-transparency-light-tile-green"></a>**Helle Felder: Grün** | Grünanteil der hellen Schachbrettfelder. Wird mit dem Schachbretthintergrund verwendet. |
| <a id="general-background-transparency-light-tile-blue"></a>**Helle Felder: Blau** | Blauanteil der hellen Schachbrettfelder. Wird mit dem Schachbretthintergrund verwendet. |
| <a id="general-background-transparency-dark-tile-red"></a>**Dunkle Felder: Rot** | Rotanteil der dunklen Schachbrettfelder. Wird mit dem Schachbretthintergrund verwendet. |
| <a id="general-background-transparency-dark-tile-green"></a>**Dunkle Felder: Grün** | Grünanteil der dunklen Schachbrettfelder. Wird mit dem Schachbretthintergrund verwendet. |
| <a id="general-background-transparency-dark-tile-blue"></a>**Dunkle Felder: Blau** | Blauanteil der dunklen Schachbrettfelder. Wird mit dem Schachbretthintergrund verwendet. |
| <a id="general-background-transparency-tile-size-pixels"></a>**Mustergröße (Pixel)** | Höher macht die einzelnen Felder im Schachbrettmuster größer. |

<a id="general-display-mode"></a>

## Anzeigemodus

Verwende nur Modi, die dein Bildschirm über den aktuellen Anschluss unterstützt. Nach dem Vollbild oder Anhalten wird der ursprüngliche Modus wiederhergestellt.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="general-display-mode-switch-display-mode-in-fullscreen"></a>**Anzeigemodus im Vollbild wechseln** | Bei **Ein** wählt FrankyRenderer im Vollbild einen passenden Modus aus „Zulässige Modi“ und stellt ihn danach zurück. Bei **Aus** bleibt der aktuelle Anzeigemodus aktiv. |
| <a id="general-display-mode-allowed-modes"></a>**Zulässige Modi** | Durch Kommas getrennt, z. B. 2160p23. Nur 23 behält die Auflösung bei. 23, 29 und 59 stehen für 23,976, 29,97 und 59,94 Hz. |
| <a id="general-display-mode-pause-after-switching-seconds"></a>**Pause nach dem Wechsel (Sekunden)** | Pausiert die Wiedergabe, während sich der Bildschirm nach einem Moduswechsel neu synchronisiert. |

<a id="general-diagnostics"></a>

## Diagnose

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="general-diagnostics-diagnostic-log"></a>**Diagnoseprotokoll** | Bei **Ein** wird der nächste Playerstart für die Fehlersuche protokolliert. Danach schaltet sich die Aufzeichnung selbst aus. Bei **Aus** startet der Player ohne diese Aufzeichnung. |
| <a id="general-diagnostics-save-report"></a>**Bericht speichern** | Speichert einen Bericht für eine Fehlermeldung auf dem Desktop. |

<a id="general-themes"></a>

## Darstellung

- **Thema** Indigo, Graphite, Midnight, Ocean oder Light.
- **Akzentfarbe** Wähle eine Farbe oder übernimm mit Windows den Systemakzent.
- **Sprache** Automatisch folgt der Windows-Anzeigesprache. Die verfügbaren Sprachen hängen vom Build ab.
- **Letzte Seite merken** Bei **Ein** kehrt das Menü zur letzten Seite, Registerkarte und Scrollposition zurück. Bei **Aus** öffnet es die Startansicht.

---

[← Startseite](DE-Home.md) · [Verarbeitung →](DE-Processing.md)
