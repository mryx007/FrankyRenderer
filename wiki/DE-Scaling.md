[English](Scaling.md) · **Deutsch**

[Startseite](DE-Home.md) · [Menü bedienen](DE-Using-the-menu.md) · [Quellen](DE-Sources.md)

Filter für Bildgröße, Farbkanten und Bewegung.

**Auf dieser Seite**

- [Filter auswählen](#choosing-a-filter)
- [Filter](#scaling-filters)
- [RTX Video Super Resolution](#scaling-rtx-video-super-resolution)
- [Benutzerdefiniert: Hochskalierung des Bildes](#scaling-custom-image-upscaler)
- [Benutzerdefiniert: Verkleinerung des Bildes](#scaling-custom-image-downscaler)
- [Benutzerdefiniert: Hochskalierung der Ebenen (Chroma / Alpha)](#scaling-custom-plane-upscaler-chroma-alpha)
- [Benutzerdefiniert: Verkleinerung der Ebenen (Chroma / Alpha)](#scaling-custom-plane-downscaler-chroma-alpha)
- [Benutzerdefiniert: Bildmischer](#scaling-custom-frame-mixer)

---

<a id="choosing-a-filter"></a>

## Filter auswählen

**Für den Einstieg lass auto eingestellt.** Wenn du etwas verbessern willst, wähle zuerst das passende Feld.

### Zuerst das richtige Feld wählen

| Was passiert? | Welches Feld? | Beispiel |
| --- | --- | --- |
| Das Videobild wird größer. | **Bild-Hochskalierer** | 1080p-Video füllt eine 4K-Fläche. |
| Das Videobild wird kleiner. | **Verkleinerung des Bildes** | 4K-Video läuft in einem 1080p-Fenster. |
| Farben werden feiner rekonstruiert. | **Hochskalierung der Ebenen (Chroma / Alpha)** | Ein 4:2:0-Video kann auch in Originalgröße von saubereren Farbkanten profitieren. |
| Benachbarte Bilder werden gemischt. | **Bildmischer** | Smooth Motion hilft, wenn ein 24-fps-Film auf 60 Hz ruckelt. |

Entscheidend ist die Größe des **Videobilds**. Ein kleines Fenster auf einem 4K-Bildschirm kann das Video auch verkleinern.

### Schnelle Empfehlungen

| Situation | Probiere | Worauf achten? |
| --- | --- | --- |
| Film von 1080p auf 4K | **auto**, dann **lanczos** | Werden Haare und Texturen klarer? |
| Mehr Schärfe beim Vergrößern | **ewa_lanczossharp** gegen **lanczos** | Werden Details klarer oder treten nur Rauschen und Säume stärker hervor? |
| Wenig GPU-Leistung | **bilinear** oder **bicubic** | Läuft es flüssiger? Ist das Bild noch scharf genug? |
| Körniges oder stark komprimiertes Video | **bicubic** gegen **lanczos** | Treten Körnung, Blockkanten oder Säume weniger hervor? |
| 4K auf 1080p verkleinern | **mitchell** gegen **lanczos** | Flimmern Ziegel oder Gitter in Bewegung? Lass „Kantenglättung beim Herunterskalieren überspringen“ auf **Aus**. |
| Pixelgrafik oder alte Spielaufnahmen | **nearest** oder **oversample** | Sollen Pixelkanten hart bleiben oder etwas weicher werden? |
| Farbkanten bei 4:2:0 | **auto**, dann **ewa_lanczos** | Sieht rote oder blaue Schrift sauberer aus? **bilinear** braucht weniger GPU-Leistung. |
| Alte 720p-Datei auf einer NVIDIA-RTX-Karte | [RTX Video Super Resolution](#scaling-rtx-video-super-resolution-rtx-video-super-resolution) **Ein** | Super Resolution muss auch in der NVIDIA-App aktiv sein. Strg+J zeigt, ob VSR läuft. |
| Kinofilm mit 24 fps ruckelt bei Schwenks auf einem 60-Hz-Monitor | [Smooth Motion aktivieren](#scaling-filters-enable-smooth-motion) **Ein** | Laufen Schwenks gleichmäßiger? Stören Doppelkonturen? |
| Doppelkonturen mit Smooth Motion | [Bildmischer](#scaling-filters-frame-mixer) **oversample** gegen **auto** | Bleiben bewegte Gesichter und Schrift scharf? |
| Helle Säume um weiße Untertitel oder den Abspann | [Globale Unterdrückung von Ringing](#scaling-filters-global-anti-ringing) erhöhen | Werden Kanten dabei zu weich? |

<a id="approximate-gpu-cost"></a>

### Ungefährer GPU-Aufwand

| Filter | Ungefährer Aufwand | Kurz erklärt |
| --- | --- | --- |
| nearest, bilinear, box, oversample, hermite, bicubic, gaussian | Sehr gering bis gering | Gut zum Vergleichen, wenn Wiedergabe ruckelt. |
| catmull_rom, mitchell, lanczos | Mittel | Mehr Spielraum für feine Details. |
| ewa_lanczos, ewa_lanczossharp | Hoch | Prüfe die Renderzeit bei großen Videoflächen. |
| ewa_lanczos4sharpest | Sehr hoch | Der aufwendigste Filter in dieser Übersicht. |
| auto, none, custom | Abhängig vom verwendeten Filter | Die Last hängt von der gewählten Methode ab. |

Fenstergröße, Bildrate und aktive Shader ändern die Last. Prüfe bei gleicher Szene und Fenstergröße die Renderzeit mit **Strg+J**. [libplacebo-Filterübersicht](https://libplacebo.org/options/#scaling)

### Wie die einzelnen Bildfilter aussehen

Klappe einen Filter auf, um seine sichtbare Wirkung zu sehen.

<details>
<summary>auto — Auswahl dem Renderer überlassen</summary>

Ein guter Ausgangspunkt. Probiere eine manuelle Alternative, wenn du konkret mehr Schärfe oder weniger Flimmern möchtest.

</details>

<details>
<summary>none — einfache Abtastung oder Filter übernehmen</summary>

Beim Vergrößern des Bildes greift die einfache GPU-Abtastung. Beim Verkleinern wird der Bild-Hochskalierer übernommen. Ebenen übernehmen den jeweiligen Bildfilter.

</details>

<details>
<summary>custom — eigenen Filter einstellen</summary>

Übernimmt einen Ausgangsfilter. Ändere einen Parameter nach dem anderen, damit du den Unterschied siehst.

</details>

<details>
<summary>nearest — harte Pixelkanten</summary>

Gut für Pixelgrafik. Bei Filmen können schräge Kanten treppenförmig werden.

</details>

<details>
<summary>bilinear — weich und schnell</summary>

Spart GPU-Zeit. Kleine Schrift und feine Texturen können weicher werden.

</details>

<details>
<summary>oversample — Pixelkanten leicht mischen</summary>

Hält den Pixel-Look, glättet aber Übergänge, wenn die neue Größe nicht genau zum Pixelraster passt.

</details>

<details>
<summary>bicubic — ruhiger Bildeindruck</summary>

Eine Alternative, wenn lanczos Rauschen oder harte Kanten zu stark betont.

</details>

<details>
<summary>gaussian — deutlich weicher</summary>

Glättet Kanten und feine Texturen. Vergleiche an Haaren oder Stoff, wie viel Detail dabei verloren geht.

</details>

<details>
<summary>catmull_rom — kräftige Kanten</summary>

Zeichnet schärfer. Achte auf helle oder dunkle Säume um Schrift.

</details>

<details>
<summary>lanczos — feine Details</summary>

Guter Vergleich zu auto für Filme. Prüfe, ob Details klarer werden oder Kanten Säume bekommen.

</details>

<details>
<summary>ewa_lanczos — aufwendigerer Filter</summary>

Für Bildvergrößerung oder Farbkanten. Vergleiche mit lanczos und prüfe die Renderzeit.

</details>

<details>
<summary>ewa_lanczossharp — schärfer als EWA Lanczos</summary>

Kann Haare und Stoff deutlicher zeichnen, aber auch Körnung oder Konturen verstärken.

</details>

<details>
<summary>ewa_lanczos4sharpest — sehr scharf und aufwendig</summary>

Vergleiche mit ewa_lanczossharp. Achte auf Rauschen, Säume und flüssige Wiedergabe.

</details>

<details>
<summary>box — einfaches Verkleinern</summary>

Mittelt Bildpunkte zusammen. Vergleiche feine Gitter mit mitchell und achte auf Flimmern.

</details>

<details>
<summary>hermite — schnelles Verkleinern</summary>

Spart GPU-Zeit. Vergleiche schräge Linien mit mitchell, wenn sie unruhig aussehen.

</details>

<details>
<summary>mitchell — ausgewogenes Verkleinern</summary>

Ein guter Vergleich zu lanczos, wenn ein feines Muster während der Bewegung flimmert.

</details>

### Bildmischer separat wählen

Smooth Motion mischt benachbarte Videobilder. Das kann Kameraschwenks glätten und dabei Doppelkonturen erzeugen.

| Wahl | Wann ausprobieren? | Worauf achten? |
| --- | --- | --- |
| **auto** | Als Startpunkt. | Der Renderer wählt den Mischer. |
| **none** | Wenn dich Doppelkonturen stören. | Zeigt ein einzelnes Quellbild. Bewegung kann ungleichmäßiger wirken. |
| **custom** | Wenn du den Mischer selbst einstellen willst. | Ändere jeweils nur einen Parameter. |
| **oversample** | Als ersten Vergleich bei 24 fps auf 60 Hz. | Mischt vor allem an Übergängen. |
| **hermite** | Für einen anderen, gewichteten Übergang. | Prüfe bewegte Gesichter oder Schrift. |
| **linear** | Für direktes Mischen benachbarter Bilder. | Bewegte Kanten können Doppelkonturen zeigen. |
| **cubic** | Wenn du über mehr Bilder mischen willst. | Prüfe, ob Bewegung ruhiger oder zu weich wirkt. |

**Clamped Mitchell** findest du als **mitchell_clamp** im Menü. Der [Schwellenwert für Oversampling](#scaling-filters-oversample-threshold) steuert, wann ein ganzes Bild bevorzugt wird. [Bilder im Voraus ausgeben](#scaling-filters-present-in-advance) beeinflusst den zeitlichen Vorlauf.

### Filtern in fünf Minuten vergleichen

1. Nimm dieselbe Szene mit Details und einem Kameraschwenk. Schalte zusätzliche Schärfung und RTX VSR für den Vergleich aus.
2. Wechsle zwischen **auto** und einem passenden Filter. Lass die Fenstergröße gleich.
3. Achte auf Flimmern, Säume und flüssige Wiedergabe. Mit **Strg+J** siehst du die Renderzeit.

---

Bei **Benutzerdefiniert** übernimmst du einen Ausgangsfilter und passt ihn an. **none** ist bei geeigneten Hauptfiltern und als Fensterfunktion verfügbar.

<a id="scaling-filters"></a>

## Filter

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="scaling-filters-image-upscaler"></a>**Bild-Hochskalierer** | Rekonstruiert Bilddetails beim Vergrößern des Videos. 'Benutzerdefiniert' übernimmt den ausgewählten Filter zur Bearbeitung. |
| <a id="scaling-filters-image-downscaler"></a>**Verkleinerung des Bildes** | Filtert das Bild beim Verkleinern des Videos. Benutzerdefiniert übernimmt den ausgewählten Filter zur Bearbeitung. |
| <a id="scaling-filters-plane-upscaler-chroma-alpha"></a>**Hochskalierung der Ebenen (Chroma / Alpha)** | Rekonstruiert unterabgetastete Ebenen für Chroma oder Alpha vor der Bildskalierung. |
| <a id="scaling-filters-plane-downscaler-chroma-alpha"></a>**Verkleinerung der Ebenen (Chroma / Alpha)** | Filtert die Ebenen für Chroma oder Alpha beim Verringern ihrer Auflösung. |
| <a id="scaling-filters-frame-mixer"></a>**Bildmischer** | Bestimmt bei Smooth Motion, wie benachbarte Videobilder gemischt werden. Mehr Mischung kann Bewegungen gleichmäßiger machen und zugleich sichtbare Doppelkonturen erzeugen. |
| <a id="scaling-filters-global-anti-ringing"></a>**Globale Unterdrückung von Ringing** | Unterdrückt helle und dunkle Säume an kontrastreichen Kanten, etwa um weiße Schrift. Hohe Stärke kann Kanten weicher wirken lassen. |
| <a id="scaling-filters-enable-smooth-motion"></a>**Smooth Motion aktivieren** | Bei **Ein** mischt der Renderer benachbarte Bilder. Das kann Ruckeln bei unpassender Bildrate verringern, aber Doppelkonturen erzeugen. Bei **Aus** bleiben die Bilder ungemischt. |
| <a id="scaling-filters-skip-downscale-anti-aliasing"></a>**Kantenglättung beim Herunterskalieren überspringen** | Bei **Aus (Standard)** bleibt die Kantenglättung beim Verkleinern aktiv. Bei **Ein** wird sie übersprungen, um GPU-Zeit zu sparen. Feine Linien und Muster können dann flimmern oder Treppenkanten zeigen. |
| <a id="scaling-filters-preserve-frame-mixer-cache"></a>**Zwischenspeicher des Bildmischers beibehalten** | Bei **Aus (Standard)** berechnet der Bildmischer nach Änderungen die Bilder neu. Bei **Ein** nutzt er alte Bilder kurz weiter. Größenwechsel wirken flüssiger, können aber kurz unschärfer sein. |
| <a id="scaling-filters-present-in-advance"></a>**Bilder im Voraus ausgeben** | Bilder, die vorab an das Display gehen. Mehr fängt Renderspitzen ab, erhöht aber die Latenz. Automatisch erhöht nur nach verspäteten Bildern. |
| <a id="scaling-filters-oversample-threshold"></a>**Schwellenwert für Oversampling** | Zeigt ein ganzes Bild statt einer Mischung, wenn das andere weniger als diesen Anteil einer Bildwiederholung abdeckt. Weniger Doppelkanten; 0 mischt immer. |

<a id="scaling-rtx-video-super-resolution"></a>

## RTX Video Super Resolution

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="scaling-rtx-video-super-resolution-rtx-video-super-resolution"></a>**RTX Video Super Resolution** | Bei **Ein** fordert FrankyRenderer beim Vergrößern KI-Skalierung vom NVIDIA-Treiber an. Super Resolution muss auch in der NVIDIA-App aktiv sein. Bei **Aus** greift der normale Skalierungsfilter. |

<a id="scaling-custom-image-upscaler"></a>

## Benutzerdefiniert: Hochskalierung des Bildes

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="scaling-custom-image-upscaler-enable-custom-settings"></a>**Benutzerdefinierte Einstellungen aktivieren** | Bei **Ein** kannst du den gewählten Ausgangsfilter anpassen. Bei **Aus** kommt der Ausgangsfilter wieder direkt zum Einsatz. |
| <a id="scaling-custom-image-upscaler-starting-filter"></a>**Ausgangsfilter** | Basisvorlage für diesen benutzerdefinierten Filter. Ihre Werte werden übernommen, bis ein Parameter geändert wird. |
| <a id="scaling-custom-image-upscaler-kernel"></a>**Filterkern** | Rekonstruktionsfunktion. Automatisch übernimmt den Filterkern des Ausgangsfilters. |
| <a id="scaling-custom-image-upscaler-window"></a>**Fensterfunktion** | Auf den Filterkern angewendete Fensterfunktion. Automatisch übernimmt sie; Keine entfernt sie. |
| <a id="scaling-custom-image-upscaler-radius"></a>**Radius** | Stützradius des Filterkerns. Nur für größenveränderliche Filterkerne verfügbar. Größere Radien tasten mehr Pixel ab. |
| <a id="scaling-custom-image-upscaler-clamp"></a>**Begrenzung** | Unterdrückt negative Filtergewichte, um Ringing zu verringern. Höhere Werte machen das Ergebnis weicher. |
| <a id="scaling-custom-image-upscaler-blur"></a>**Unschärfe** | Multiplikator für die Filterbreite. Unter 1 wird das Bild schärfer, über 1 weicher. 0 verwendet den Standardwert des Filters. |
| <a id="scaling-custom-image-upscaler-taper"></a>**Abflachung** | Flacht das Zentrum des Rekonstruktionskerns ab. |
| <a id="scaling-custom-image-upscaler-anti-ringing"></a>**Unterdrückung von Ringing** | Unterdrückt Überschwinger für diesen Filter. 0 übernimmt die globale Stärke der Unterdrückung von Ringing. |
| <a id="scaling-custom-image-upscaler-kernel-parameter-1"></a>**Filterkernparameter 1** | Erster filterspezifischer Parameter. Nur verfügbar, wenn der ausgewählte Filterkern ihn unterstützt. |
| <a id="scaling-custom-image-upscaler-kernel-parameter-2"></a>**Filterkernparameter 2** | Zweiter filterspezifischer Parameter. Nur verfügbar, wenn der ausgewählte Filterkern ihn unterstützt. |
| <a id="scaling-custom-image-upscaler-window-parameter-1"></a>**Fensterparameter 1** | Erster fensterspezifischer Parameter. Nur verfügbar, wenn die ausgewählte Fensterfunktion ihn unterstützt. |
| <a id="scaling-custom-image-upscaler-window-parameter-2"></a>**Fensterparameter 2** | Zweiter fensterspezifischer Parameter. Nur verfügbar, wenn die ausgewählte Fensterfunktion ihn unterstützt. |
| <a id="scaling-custom-image-upscaler-polar-ewa"></a>**Polar (EWA)** | Bei **Ein** werden benachbarte Pixel in zwei Richtungen gemeinsam gewichtet (EWA). Bei **Aus** filtert der Renderer waagerecht und senkrecht getrennt. **Ausgangsfilter übernehmen** folgt der Vorlage. |

<a id="scaling-custom-image-downscaler"></a>

## Benutzerdefiniert: Verkleinerung des Bildes

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="scaling-custom-image-downscaler-enable-custom-settings"></a>**Benutzerdefinierte Einstellungen aktivieren** | Bei **Ein** kannst du den gewählten Ausgangsfilter anpassen. Bei **Aus** kommt der Ausgangsfilter wieder direkt zum Einsatz. |
| <a id="scaling-custom-image-downscaler-starting-filter"></a>**Ausgangsfilter** | Basisvorlage für diesen benutzerdefinierten Filter. Ihre Werte werden übernommen, bis ein Parameter geändert wird. |
| <a id="scaling-custom-image-downscaler-kernel"></a>**Filterkern** | Rekonstruktionsfunktion. Automatisch übernimmt den Filterkern des Ausgangsfilters. |
| <a id="scaling-custom-image-downscaler-window"></a>**Fensterfunktion** | Auf den Filterkern angewendete Fensterfunktion. Automatisch übernimmt sie; Keine entfernt sie. |
| <a id="scaling-custom-image-downscaler-radius"></a>**Radius** | Stützradius des Filterkerns. Nur für größenveränderliche Filterkerne verfügbar. Größere Radien tasten mehr Pixel ab. |
| <a id="scaling-custom-image-downscaler-clamp"></a>**Begrenzung** | Unterdrückt negative Filtergewichte, um Ringing zu verringern. Höhere Werte machen das Ergebnis weicher. |
| <a id="scaling-custom-image-downscaler-blur"></a>**Unschärfe** | Multiplikator für die Filterbreite. Unter 1 wird das Bild schärfer, über 1 weicher. 0 verwendet den Standardwert des Filters. |
| <a id="scaling-custom-image-downscaler-taper"></a>**Abflachung** | Flacht das Zentrum des Rekonstruktionskerns ab. |
| <a id="scaling-custom-image-downscaler-anti-ringing"></a>**Unterdrückung von Ringing** | Unterdrückt Überschwinger für diesen Filter. 0 übernimmt die globale Stärke der Unterdrückung von Ringing. |
| <a id="scaling-custom-image-downscaler-kernel-parameter-1"></a>**Filterkernparameter 1** | Erster filterspezifischer Parameter. Nur verfügbar, wenn der ausgewählte Filterkern ihn unterstützt. |
| <a id="scaling-custom-image-downscaler-kernel-parameter-2"></a>**Filterkernparameter 2** | Zweiter filterspezifischer Parameter. Nur verfügbar, wenn der ausgewählte Filterkern ihn unterstützt. |
| <a id="scaling-custom-image-downscaler-window-parameter-1"></a>**Fensterparameter 1** | Erster fensterspezifischer Parameter. Nur verfügbar, wenn die ausgewählte Fensterfunktion ihn unterstützt. |
| <a id="scaling-custom-image-downscaler-window-parameter-2"></a>**Fensterparameter 2** | Zweiter fensterspezifischer Parameter. Nur verfügbar, wenn die ausgewählte Fensterfunktion ihn unterstützt. |
| <a id="scaling-custom-image-downscaler-polar-ewa"></a>**Polar (EWA)** | Bei **Ein** werden benachbarte Pixel in zwei Richtungen gemeinsam gewichtet (EWA). Bei **Aus** filtert der Renderer waagerecht und senkrecht getrennt. **Ausgangsfilter übernehmen** folgt der Vorlage. |

<a id="scaling-custom-plane-upscaler-chroma-alpha"></a>

## Benutzerdefiniert: Hochskalierung der Ebenen (Chroma / Alpha)

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-enable-custom-settings"></a>**Benutzerdefinierte Einstellungen aktivieren** | Bei **Ein** kannst du den gewählten Ausgangsfilter anpassen. Bei **Aus** kommt der Ausgangsfilter wieder direkt zum Einsatz. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-starting-filter"></a>**Ausgangsfilter** | Basisvorlage für diesen benutzerdefinierten Filter. Ihre Werte werden übernommen, bis ein Parameter geändert wird. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-kernel"></a>**Filterkern** | Rekonstruktionsfunktion. Automatisch übernimmt den Filterkern des Ausgangsfilters. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-window"></a>**Fensterfunktion** | Auf den Filterkern angewendete Fensterfunktion. Automatisch übernimmt sie; Keine entfernt sie. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-radius"></a>**Radius** | Stützradius des Filterkerns. Nur für größenveränderliche Filterkerne verfügbar. Größere Radien tasten mehr Pixel ab. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-clamp"></a>**Begrenzung** | Unterdrückt negative Filtergewichte, um Ringing zu verringern. Höhere Werte machen das Ergebnis weicher. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-blur"></a>**Unschärfe** | Multiplikator für die Filterbreite. Unter 1 wird das Bild schärfer, über 1 weicher. 0 verwendet den Standardwert des Filters. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-taper"></a>**Abflachung** | Flacht das Zentrum des Rekonstruktionskerns ab. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-anti-ringing"></a>**Unterdrückung von Ringing** | Unterdrückt Überschwinger für diesen Filter. 0 übernimmt die globale Stärke der Unterdrückung von Ringing. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-kernel-parameter-1"></a>**Filterkernparameter 1** | Erster filterspezifischer Parameter. Nur verfügbar, wenn der ausgewählte Filterkern ihn unterstützt. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-kernel-parameter-2"></a>**Filterkernparameter 2** | Zweiter filterspezifischer Parameter. Nur verfügbar, wenn der ausgewählte Filterkern ihn unterstützt. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-window-parameter-1"></a>**Fensterparameter 1** | Erster fensterspezifischer Parameter. Nur verfügbar, wenn die ausgewählte Fensterfunktion ihn unterstützt. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-window-parameter-2"></a>**Fensterparameter 2** | Zweiter fensterspezifischer Parameter. Nur verfügbar, wenn die ausgewählte Fensterfunktion ihn unterstützt. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-polar-ewa"></a>**Polar (EWA)** | Bei **Ein** werden benachbarte Pixel in zwei Richtungen gemeinsam gewichtet (EWA). Bei **Aus** filtert der Renderer waagerecht und senkrecht getrennt. **Ausgangsfilter übernehmen** folgt der Vorlage. |

<a id="scaling-custom-plane-downscaler-chroma-alpha"></a>

## Benutzerdefiniert: Verkleinerung der Ebenen (Chroma / Alpha)

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-enable-custom-settings"></a>**Benutzerdefinierte Einstellungen aktivieren** | Bei **Ein** kannst du den gewählten Ausgangsfilter anpassen. Bei **Aus** kommt der Ausgangsfilter wieder direkt zum Einsatz. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-starting-filter"></a>**Ausgangsfilter** | Basisvorlage für diesen benutzerdefinierten Filter. Ihre Werte werden übernommen, bis ein Parameter geändert wird. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-kernel"></a>**Filterkern** | Rekonstruktionsfunktion. Automatisch übernimmt den Filterkern des Ausgangsfilters. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-window"></a>**Fensterfunktion** | Auf den Filterkern angewendete Fensterfunktion. Automatisch übernimmt sie; Keine entfernt sie. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-radius"></a>**Radius** | Stützradius des Filterkerns. Nur für größenveränderliche Filterkerne verfügbar. Größere Radien tasten mehr Pixel ab. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-clamp"></a>**Begrenzung** | Unterdrückt negative Filtergewichte, um Ringing zu verringern. Höhere Werte machen das Ergebnis weicher. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-blur"></a>**Unschärfe** | Multiplikator für die Filterbreite. Unter 1 wird das Bild schärfer, über 1 weicher. 0 verwendet den Standardwert des Filters. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-taper"></a>**Abflachung** | Flacht das Zentrum des Rekonstruktionskerns ab. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-anti-ringing"></a>**Unterdrückung von Ringing** | Unterdrückt Überschwinger für diesen Filter. 0 übernimmt die globale Stärke der Unterdrückung von Ringing. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-kernel-parameter-1"></a>**Filterkernparameter 1** | Erster filterspezifischer Parameter. Nur verfügbar, wenn der ausgewählte Filterkern ihn unterstützt. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-kernel-parameter-2"></a>**Filterkernparameter 2** | Zweiter filterspezifischer Parameter. Nur verfügbar, wenn der ausgewählte Filterkern ihn unterstützt. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-window-parameter-1"></a>**Fensterparameter 1** | Erster fensterspezifischer Parameter. Nur verfügbar, wenn die ausgewählte Fensterfunktion ihn unterstützt. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-window-parameter-2"></a>**Fensterparameter 2** | Zweiter fensterspezifischer Parameter. Nur verfügbar, wenn die ausgewählte Fensterfunktion ihn unterstützt. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-polar-ewa"></a>**Polar (EWA)** | Bei **Ein** werden benachbarte Pixel in zwei Richtungen gemeinsam gewichtet (EWA). Bei **Aus** filtert der Renderer waagerecht und senkrecht getrennt. **Ausgangsfilter übernehmen** folgt der Vorlage. |

<a id="scaling-custom-frame-mixer"></a>

## Benutzerdefiniert: Bildmischer

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="scaling-custom-frame-mixer-enable-custom-settings"></a>**Benutzerdefinierte Einstellungen aktivieren** | Bei **Ein** kannst du den gewählten Ausgangsfilter anpassen. Bei **Aus** kommt der Ausgangsfilter wieder direkt zum Einsatz. |
| <a id="scaling-custom-frame-mixer-starting-filter"></a>**Ausgangsfilter** | Basisvorlage für diesen benutzerdefinierten Filter. Ihre Werte werden übernommen, bis ein Parameter geändert wird. |
| <a id="scaling-custom-frame-mixer-kernel"></a>**Filterkern** | Rekonstruktionsfunktion. Automatisch übernimmt den Filterkern des Ausgangsfilters. |
| <a id="scaling-custom-frame-mixer-window"></a>**Fensterfunktion** | Auf den Filterkern angewendete Fensterfunktion. Automatisch übernimmt sie; Keine entfernt sie. |
| <a id="scaling-custom-frame-mixer-radius"></a>**Radius** | Stützradius des Filterkerns. Nur für größenveränderliche Filterkerne verfügbar. Größere Radien tasten mehr Pixel ab. |
| <a id="scaling-custom-frame-mixer-clamp"></a>**Begrenzung** | Unterdrückt negative Filtergewichte, um Ringing zu verringern. Höhere Werte machen das Ergebnis weicher. |
| <a id="scaling-custom-frame-mixer-blur"></a>**Unschärfe** | Multiplikator für die Filterbreite. Unter 1 wird das Bild schärfer, über 1 weicher. 0 verwendet den Standardwert des Filters. |
| <a id="scaling-custom-frame-mixer-taper"></a>**Abflachung** | Flacht das Zentrum des Rekonstruktionskerns ab. |
| <a id="scaling-custom-frame-mixer-anti-ringing"></a>**Unterdrückung von Ringing** | Unterdrückt Überschwinger für diesen Filter. 0 übernimmt die globale Stärke der Unterdrückung von Ringing. |
| <a id="scaling-custom-frame-mixer-kernel-parameter-1"></a>**Filterkernparameter 1** | Erster filterspezifischer Parameter. Nur verfügbar, wenn der ausgewählte Filterkern ihn unterstützt. |
| <a id="scaling-custom-frame-mixer-kernel-parameter-2"></a>**Filterkernparameter 2** | Zweiter filterspezifischer Parameter. Nur verfügbar, wenn der ausgewählte Filterkern ihn unterstützt. |
| <a id="scaling-custom-frame-mixer-window-parameter-1"></a>**Fensterparameter 1** | Erster fensterspezifischer Parameter. Nur verfügbar, wenn die ausgewählte Fensterfunktion ihn unterstützt. |
| <a id="scaling-custom-frame-mixer-window-parameter-2"></a>**Fensterparameter 2** | Zweiter fensterspezifischer Parameter. Nur verfügbar, wenn die ausgewählte Fensterfunktion ihn unterstützt. |
| <a id="scaling-custom-frame-mixer-polar-ewa"></a>**Polar (EWA)** | Beim Bildmischer ist diese Auswahl ausgegraut. Er bleibt eindimensional und mischt Bilder über die Zeit. |

---

[← Verarbeitung](DE-Processing.md) · [Schwarze Balken / Zoom →](DE-Black-Bars-and-Zoom.md)
