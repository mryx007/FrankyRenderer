[English](HDR-and-Color-Mapping.md) · **Deutsch**

[Startseite](DE-Home.md) · [Menü bedienen](DE-Using-the-menu.md) · [Quellen](DE-Sources.md)

**Menüpfad** HDR. Tone Mapping passt die Helligkeit an. Gamut Mapping passt den Farbumfang an.

**Auf dieser Seite**

- [Typische Situationen](#hdr-color-mapping-examples)
- [Ausgabe von HDR](#hdr-color-mapping-hdr-output)
  - [Welche Tone-Mapping-Funktion soll ich wählen?](#choosing-a-tone-mapping-function)
- [Erkennung der Spitzenhelligkeit für HDR](#hdr-color-mapping-hdr-peak-detection)
- [Knickpunkt des Tone Mappings](#hdr-color-mapping-tone-mapping-knee)
- [Konstanten für Tone Mapping](#hdr-color-mapping-tone-mapping-constants)
- [Kontrastwiederherstellung](#hdr-color-mapping-contrast-recovery)
- [Konstanten für Gamut Mapping](#hdr-color-mapping-gamut-mapping-constants)
- [Qualität der LUTs für die Farbumrechnung](#hdr-color-mapping-mapping-lut-quality)
- [Visualisierung der Farbumrechnung](#hdr-color-mapping-mapping-visualization)
- [Erweiterung und Metadaten](#hdr-color-mapping-expansion-metadata)
- [Tiefen](#hdr-color-mapping-shadows)
- [Mitteltöne](#hdr-color-mapping-midtones)
- [Lichter](#hdr-color-mapping-highlights)
- [Tonwertregler](#hdr-color-mapping-tone-controls)

<a id="hdr-color-mapping-examples"></a>

## Typische Situationen

Beispiele aus dem Alltag. Ändere jeweils nur eine Einstellung und vergleiche dieselbe Szene.

| Situation | Einstellung | Worauf achten? |
| --- | --- | --- |
| HDR-Film auf einem normalen Monitor ohne HDR. | [Modus für HDR](#hdr-color-mapping-hdr-output-hdr-mode) **HDR in SDR umrechnen**. | Gesichter sollen natürlich wirken, helle Fenster Zeichnung behalten. |
| Bei der Umrechnung in SDR wirken Gesichter zu dunkel. | [Funktion für Tone Mapping](#hdr-color-mapping-hdr-output-tone-mapping-function) **BT.2446A** mit **Automatisch (Metadaten)** vergleichen. | Vergleiche dieselbe angehaltene Szene. |
| HDR-Fernseher mit gutem eigenem Tone Mapping, etwa ein OLED-TV. | [Modus für HDR](#hdr-color-mapping-hdr-output-hdr-mode) **HDR an den Bildschirm weitergeben**. | Der Fernseher passt die Helligkeit selbst an. |
| HDR-Monitor ohne eigenes Tone Mapping: Helle Wolken und Lampen fressen aus. | **HDR an die Spitzenhelligkeit des Bildschirms anpassen**, [Zielspitzenhelligkeit](#hdr-color-mapping-hdr-output-hdr-target-peak-0-auto) 0 oder der Wert des Monitors, etwa 400. | Wolken sollen Struktur zeigen. |
| Windows HDR soll nur beim Filmschauen eingeschaltet sein. | [Windows-HDR umschalten](#hdr-color-mapping-hdr-output-switch-windows-hdr) **Einschalten** oder **Im Vollbild einschalten**. | Nach der Wiedergabe kehrt Windows zum vorherigen Zustand zurück. |
| Normale Videos sollen auf dem HDR-Bildschirm kräftiger leuchten (NVIDIA RTX). | [RTX Video HDR](#hdr-color-mapping-hdr-output-rtx-video-hdr) **Ein**. | Windows HDR und RTX Video HDR in der NVIDIA-App müssen an sein. |
| Normale Videos wirken bei eingeschaltetem Windows HDR zu dunkel. | [Zielhelligkeit für SDR](#hdr-color-mapping-hdr-output-sdr-target-brightness-nits) erhöhen, etwa von 203 auf 250. | Weiß soll hell, aber nicht grell wirken. |
| Eine Nachtszene mit einer einzelnen hellen Lampe wirkt insgesamt zu dunkel. | [Perzentil](#hdr-color-mapping-hdr-peak-detection-percentile) etwas senken, etwa auf 99,99. | Gilt nur, wenn FrankyRenderer das Tone Mapping übernimmt. |
| Die Helligkeit pumpt bei Szenenwechseln sichtbar. | [Glättungszeitraum](#hdr-color-mapping-hdr-peak-detection-smoothing-period) erhöhen. | Die Anpassung wird ruhiger, aber langsamer. |
| Dunkle HDR-Szenen sind zu finster, der Rest passt. | Unter [Tiefen](#hdr-color-mapping-shadows) die **Belichtung** etwa auf +0,3. | Schwarz soll nicht grau werden. |
| Ein Dolby-Vision-Film (Profil 7 oder 8) sieht anders aus als erwartet. | [Dolby Vision verwenden](#hdr-color-mapping-hdr-output-use-dolby-vision) zum Vergleich **Aus**. | Dann läuft die HDR10- oder HLG-Basisschicht. |

<a id="hdr-color-mapping-hdr-output"></a>

## Ausgabe von HDR

| Ziel | Passender Modus |
| --- | --- |
| HDR dem Bildschirm überlassen | HDR durchreichen |
| HDR-Spitzen an die Helligkeit des Bildschirms anpassen | HDR an die Spitzenhelligkeit des Bildschirms anpassen |
| HDR auf einem SDR-Bildschirm ansehen | HDR in SDR umrechnen |

Unter **Strg+J** siehst du den aktiven HDR-Pfad und die gewählte Tone-Mapping-Kurve.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="hdr-color-mapping-hdr-output-hdr-mode"></a>**Modus für HDR** | HDR an das Display weitergeben; HDR behalten, aber Spitzlichter über der Display-Spitze absenken, für Bildschirme ohne eigenes Tone Mapping; oder in SDR umrechnen. |
| <a id="hdr-color-mapping-hdr-output-clipping-protection"></a><a id="hdr-color-mapping-hdr-output-sdr-target-brightness-nits"></a>**Zielhelligkeit für SDR (Nits)** | Zielleuchtdichte für die Umrechnung von HDR in SDR und das Referenzweiß für SDR bei der Ausgabe von HDR. |
| <a id="hdr-color-mapping-hdr-output-tone-mapping-function"></a>**Funktion für Tone Mapping** | Legt fest, wie der Renderer helle Bildteile in den verfügbaren Helligkeitsbereich einpasst. Die [Auswahlhilfe](#choosing-a-tone-mapping-function) erklärt, wann welche Kurve sinnvoll ist. |
| <a id="hdr-color-mapping-hdr-output-gamut-mapping-function"></a>**Funktion für Gamut Mapping** | Überführt die Quellfarben in den unterstützten Farbumfang des Ausgabebildschirms. |
| <a id="hdr-color-mapping-hdr-output-hdr-target-peak-0-auto"></a>**Zielspitzenhelligkeit für HDR (0 = automatisch)** | Gibt dem Renderer die Helligkeit vor, bis zu der er HDR-Spitzlichter abbildet. 0 übernimmt die Angabe von Windows. Der Regler ändert die Helligkeit des Panels nicht. |
| <a id="hdr-color-mapping-hdr-output-switch-windows-hdr"></a>**Windows-HDR umschalten** | Bei **Nicht ändern** bleibt Windows HDR wie eingestellt. Die Einschaltmodi können es bei HDR-Wiedergabe aktivieren. Die Ein-/Ausschaltmodi können es bei SDR auch abschalten. Vollbildvarianten schalten HDR erst im Vollbild ein. Nach der Wiedergabe wird der ursprüngliche Zustand wiederhergestellt. |
| <a id="hdr-color-mapping-hdr-output-rtx-video-hdr"></a>**RTX Video HDR** | Bei **Ein** fordert der Renderer SDR-zu-HDR-Umwandlung vom NVIDIA-Treiber an. Dafür müssen Windows HDR und RTX Video HDR in der NVIDIA-App aktiv sein. Bei **Aus** entfällt diese Umwandlung durch den Treiber. |
| <a id="hdr-color-mapping-hdr-output-use-dolby-vision"></a>**Dolby Vision verwenden** | Bei **Ein** verarbeitet FrankyRenderer Dolby Vision. Bei **Aus** spielen Profile 7 und 8 ihre kompatible HDR10- oder HLG-Basisschicht ab. Profil 5 nutzt bei beiden Stellungen Dolby Vision. |

<a id="choosing-a-tone-mapping-function"></a>

### Welche Tone-Mapping-Funktion soll ich wählen?

**Kurz gesagt:** Lass **Automatisch (Metadaten)** eingestellt. Für HDR auf einem SDR-Bildschirm lohnt sich ein Vergleich mit **BT.2446A**. Bei HDR-Durchreichung übernimmt der Bildschirm das Tone Mapping.

| Funktion | Wann sie hilft und was sich ändert |
| --- | --- |
| **Automatisch (Metadaten)** | Erste Wahl. Nutzt meist Spline und schaltet bei passenden HDR10+-Metadaten auf ST 2094-40. Unter **Strg+J → Tone mapping** siehst du die gewählte Kurve. |
| **Spline** | Wenn du eine Kurve fest vorgeben willst, auch bei HDR10+. Glättet helle Übergänge. Mit **Kontrast für Spline** bestimmst du den Kompromiss zwischen kräftigen Mitteltönen und Zeichnung in den Lichtern. |
| **BT.2390** | Wenn Wolken oder Reflexe auf deinem HDR-Bildschirm zu hell sind und der Rest schon passt. Senkt helle Spitzen sanft ab. |
| **BT.2446A** | Für HDR→SDR. Vergleiche mit Automatisch, wenn Gesichter zu dunkel wirken oder helle Flächen Details verlieren. |
| **Reinhard** | Für eine gleichmäßige Kompression. Glättet helle Spitzen, kann aber auch Mitteltöne dämpfen. |
| **Mobius** | Wenn Reinhard zu viel vom Bild verändert. Lässt dunklere Werte bis zum einstellbaren Knickpunkt weitgehend in Ruhe. |
| **Hable** | Für eine filmische Helligkeitskurve. Die Umbildung kann Gesichter und Schatten sichtbar abdunkeln. |
| **ST 2094-40 (HDR10+)** | Nutzt die mitgelieferte HDR10+-Kurve und passt sie an die Zielhelligkeit an. Fehlt sie, verwendet libplacebo eine Ersatzkurve. |
| **ST 2094-10** | Alternative für einen Szenenvergleich. Bezieht die mittlere Szenenhelligkeit in die Anpassung ein. |
| **Abschneiden** | Zum Prüfen, was ohne sanfte Kompression verloren geht. Alles oberhalb der Zielgrenze wird gleich hell. |
| **Gamma** | Ältere Kurve für einen Vergleich. Der Übergang zur Kompression ist abrupter und das Bild kann flacher wirken. |
| **Linear** | Skaliert den gesamten Bereich auf der HDR-Helligkeitsskala PQ. Verändert auch die Mitteltöne, oft deutlich. |
| **Lineares Licht** | Für kleine Änderungen der Zielspitze. Skaliert die tatsächliche Leuchtdichte proportional und kann bei HDR→SDR das ganze Bild stark abdunkeln. |

**Schnelltest:** Halte ein Bild mit einem Gesicht und einem hellen Fenster an. Ändere nur die Kurve. Bleiben Hauttöne natürlich und Details im Fenster sichtbar? [Technische Beschreibung in libplacebo](https://github.com/haasn/libplacebo/blob/29a56f9290bacc3f7c6bd899ac7a64ec4e68c8e2/docs/options.md#tone_mappingfunction)

<a id="hdr-color-mapping-hdr-peak-detection"></a>

## Erkennung der Spitzenhelligkeit für HDR

Die Glättung und Szenenschwellen bestimmen, wie schnell die geschätzte Spitzenhelligkeit auf Änderungen reagiert.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="hdr-color-mapping-hdr-peak-detection-show-clipping"></a><a id="hdr-color-mapping-hdr-output-show-clipping"></a>**Übersteuerung anzeigen** | Bei **Ein** markiert eine Diagnoseanzeige auffällige Pixel außerhalb des verarbeiteten Helligkeits- oder Farbbereichs. Bei **Aus** siehst du das normale Bild. |
| <a id="hdr-color-mapping-hdr-peak-detection-enable-peak-detection"></a>**Spitzenhelligkeitserkennung aktivieren** | Bei **Ein** kann der Renderer die Szenenhelligkeit für das Tone Mapping messen. Bei **Aus** entfällt die Messung. Dann richtet sich das Mapping nach den verfügbaren Metadaten. |
| <a id="hdr-color-mapping-hdr-peak-detection-allow-delayed-peak-detection"></a>**Verzögerte Spitzenhelligkeitserkennung zulassen** | Bei **Ein** darf die Messung ein Bild später einfließen. Das kann GPU-Wartezeit sparen, aber nach einem harten Schnitt kurz die alte Helligkeit verwenden. Bei **Aus** gilt die Messung für das aktuelle Bild. |
| <a id="hdr-color-mapping-hdr-peak-detection-smoothing-period"></a>**Glättungszeitraum** | Glättet die Messwerte der Spitzenhelligkeit über die Zeit. Höhere Werte verlangsamen die Anpassung. |
| <a id="hdr-color-mapping-hdr-peak-detection-scene-threshold-low"></a>**Untere Szenenwechselschwelle** | Legt fest, ab welchem Helligkeitssprung die Messung schneller reagiert. Niedriger reagiert schon auf kleinere Änderungen. Höher glättet mehr. |
| <a id="hdr-color-mapping-hdr-peak-detection-scene-threshold-high"></a>**Obere Szenenwechselschwelle** | Legt fest, ab welchem Helligkeitssprung die Glättung ganz aussetzt. Niedriger übernimmt die neue Helligkeit schon bei kleineren Szenensprüngen sofort. |
| <a id="hdr-color-mapping-hdr-peak-detection-percentile"></a>**Perzentil** | Bei 100 zählt die hellste gemessene Stelle. Ein kleinerer Wert gibt winzigen Lichtpunkten weniger Einfluss, etwa einer einzelnen Straßenlaterne in einer Nachtszene. |
| <a id="hdr-color-mapping-hdr-peak-detection-black-cutoff"></a>**Schwarzuntergrenze** | Schließt sehr dunkle Pixel aus der Statistik zur Erkennung der Spitzenhelligkeit in HDR aus. |

<a id="hdr-color-mapping-tone-mapping-knee"></a>

## Knickpunkt des Tone Mappings

Der Knickpunkt legt fest, um welche Helligkeit die Kurve ihren Verlauf ändert. Bei Spline und den adaptiven ST-2094-Kurven kann er von Szene zu Szene wandern. Wenn dir vor allem helle Details fehlen, vergleiche zuerst die [Tone-Mapping-Funktionen](#choosing-a-tone-mapping-function).

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="hdr-color-mapping-tone-mapping-knee-knee-adaptation"></a>**Knickpunktanpassung** | Höher passt die Helligkeit um den Knickpunkt stärker an den Zielbereich an. Niedriger hält sie eher nahe an der Quelle. Vergleiche mit Spline ein Gesicht in einer dunklen und einer hellen Szene. |
| <a id="hdr-color-mapping-tone-mapping-knee-knee-minimum"></a>**Minimaler Knickpunkt** | Begrenzt den Knickpunkt nach unten. Höher hindert dunkle Szenen daran, ihn sehr weit in die Schatten zu verschieben. |
| <a id="hdr-color-mapping-tone-mapping-knee-knee-maximum"></a>**Maximaler Knickpunkt** | Begrenzt den Knickpunkt nach oben. Niedriger hindert helle Szenen daran, ihn sehr weit in die Lichter zu verschieben. |
| <a id="hdr-color-mapping-tone-mapping-knee-knee-default"></a>**Standardknickpunkt** | Bestimmt, wo der Knickpunkt liegt, wenn die mittlere Szenenhelligkeit fehlt. Höher verschiebt diesen Ersatzpunkt zu helleren Bildteilen. |
| <a id="hdr-color-mapping-tone-mapping-knee-bt-2390-knee-offset"></a>**Knickpunktversatz für BT.2390** | Wirkt nur bei BT.2390. Höher lässt die Kompression bei niedrigeren Helligkeiten beginnen. Niedriger lässt mehr mittlere und helle Töne bis zum Übergang näher am Original. |

<a id="hdr-color-mapping-tone-mapping-constants"></a>

## Konstanten für Tone Mapping

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="hdr-color-mapping-tone-mapping-constants-spline-slope-tuning"></a>**Steigungsanpassung für Spline** | Bestimmt mit, wie stark der Unterschied zwischen Quell- und Zielspitze den Kontrast um den Knickpunkt beeinflusst. Die Wirkung hängt vom Bild und der Zielhelligkeit ab. |
| <a id="hdr-color-mapping-tone-mapping-constants-spline-slope-offset"></a>**Steigungsversatz für Spline** | Verschiebt die Grundanpassung der Spline-Steigung. Das verändert den Kontrast um den Knickpunkt, auch wenn Quell- und Zielspitze nahe beieinanderliegen. |
| <a id="hdr-color-mapping-tone-mapping-constants-spline-contrast"></a>**Kontrast für Spline** | Höher macht die Mitteltöne kontrastreicher. Niedriger lässt mehr Abstufungen in Lichtern und Tiefen sichtbar. Wirkt bei Spline, auch wenn Automatisch diese Kurve auswählt. |
| <a id="hdr-color-mapping-tone-mapping-constants-reinhard-contrast"></a>**Kontrast für Reinhard** | Höher hellt mittlere Werte relativ zur Zielspitze auf. Niedriger hält diese Töne dunkler. Wirkt nur bei Reinhard. |
| <a id="hdr-color-mapping-tone-mapping-constants-mobius-gamma-knee"></a>**Knickpunkt für Mobius / Gamma** | Höher lässt die Kompression erst bei helleren Bildteilen einsetzen. Niedriger bezieht mehr mittlere Töne ein. Wirkt nur bei Mobius und Gamma. |
| <a id="hdr-color-mapping-tone-mapping-constants-linear-exposure"></a>**Lineare Belichtung** | Ändert die Helligkeit bei Linear und Lineares Licht. Höher hellt auf, niedriger dunkelt ab. Bei 1 greift keine zusätzliche Verstärkung. |

<a id="hdr-color-mapping-contrast-recovery"></a>

## Kontrastwiederherstellung

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="hdr-color-mapping-contrast-recovery-contrast-recovery"></a>**Kontrastwiederherstellung** | Stellt lokalen Kontrast wieder her, der beim Tone Mapping verloren geht. 0 deaktiviert die Wiederherstellung. |
| <a id="hdr-color-mapping-contrast-recovery-contrast-smoothness"></a>**Kontrastglättung** | Räumlicher Maßstab für die Kontrastwiederherstellung. Wird nur verwendet, wenn die Wiederherstellung über 0 liegt. |

<a id="hdr-color-mapping-gamut-mapping-constants"></a>

## Konstanten für Gamut Mapping

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="hdr-color-mapping-gamut-mapping-constants-perceptual-deadzone"></a>**Wahrnehmungsbezogener Schonbereich** | Schützt Farben, die bereits innerhalb des Zielfarbumfangs liegen, vor wahrnehmungsorientierter Neuzuordnung. |
| <a id="hdr-color-mapping-gamut-mapping-constants-perceptual-strength"></a>**Stärke der Wahrnehmungsanpassung** | Gewichtet die wahrnehmungsorientierte Farbumfangskompression gegenüber der farbmetrischen Zuordnung. |
| <a id="hdr-color-mapping-gamut-mapping-constants-colorimetric-gamma"></a>**Farbmetrisches Gamma** | Gamma für die farbmetrische Begrenzung. Wirkt auf Relativ und verwandte Umrechnungsverfahren. |
| <a id="hdr-color-mapping-gamut-mapping-constants-softclip-knee"></a>**Knickpunkt für Softclip** | Sättigungsschwelle, ab der die weiche Begrenzung beginnt. |
| <a id="hdr-color-mapping-gamut-mapping-constants-softclip-desaturation"></a>**Entsättigung für Softclip** | Entsättigung nahe der Farbumfangsgrenze bei Verwendung von Softclip. |

<a id="hdr-color-mapping-mapping-lut-quality"></a>

## Qualität der LUTs für die Farbumrechnung

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="hdr-color-mapping-mapping-lut-quality-gamut-lut-intensity-size"></a>**LUT für den Farbumfang: Auflösung der Intensitätsachse** | Interne Auflösung der LUT für den Farbumfang entlang der Intensitätsachse. 0 verwendet die Vorgabe der Bibliothek. |
| <a id="hdr-color-mapping-mapping-lut-quality-gamut-lut-chroma-size"></a>**LUT für den Farbumfang: Auflösung der Chromaachse** | Interne Auflösung der LUT für den Farbumfang entlang der Chromaachse. 0 verwendet die Vorgabe der Bibliothek. |
| <a id="hdr-color-mapping-mapping-lut-quality-gamut-lut-hue-size"></a>**LUT für den Farbumfang: Auflösung der Farbtonachse** | Interne Auflösung der LUT für den Farbumfang entlang der Farbtonachse. 0 verwendet die Vorgabe der Bibliothek. |
| <a id="hdr-color-mapping-mapping-lut-quality-tricubic-gamut-interpolation"></a>**Trikubische Interpolation des Farbumfangs** | Bei **Ein** werden Farbwerte zwischen den Einträgen der LUT aufwendiger berechnet. Das kann Farbübergänge glatter machen, kostet aber GPU-Zeit. **Aus** verwendet die einfachere Berechnung. |
| <a id="hdr-color-mapping-mapping-lut-quality-tone-mapping-lut-size"></a>**Größe der LUT für Tone Mapping** | Interne Auflösung der LUT für die Tonwertkurve. 0 verwendet die Vorgabe der Bibliothek. |

<a id="hdr-color-mapping-mapping-visualization"></a>

## Visualisierung der Farbumrechnung

Diese Anzeigen helfen bei der Kontrolle der Zuordnung. Schalte sie für normale Wiedergabe aus.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="hdr-color-mapping-mapping-visualization-force-tone-mapping-lut"></a>**LUT für Tone Mapping erzwingen** | Bei **Ein** rechnet auch eine einfache Helligkeitskurve über eine Wertetabelle (LUT). Bei **Aus** dürfen einfache Kurven direkt berechnet werden. Die Option dient dem Vergleich der Rechenwege. |
| <a id="hdr-color-mapping-mapping-visualization-visualize-mapping-lut"></a>**LUT für die Farbumrechnung visualisieren** | Bei **Ein** liegt eine Grafik zur Helligkeits- und Farbzuordnung über dem Video. Bei **Aus** bleibt das Video frei von dieser Anzeige. |
| <a id="hdr-color-mapping-mapping-visualization-visualization-left"></a>**Visualisierung: Links** | Linker Rand der Visualisierung in normalisierten Ausgabekoordinaten (0 bis 1). |
| <a id="hdr-color-mapping-mapping-visualization-visualization-top"></a>**Visualisierung: Oben** | Oberer Rand der Visualisierung in normalisierten Ausgabekoordinaten (0 bis 1). |
| <a id="hdr-color-mapping-mapping-visualization-visualization-right"></a>**Visualisierung: Rechts** | Rechter Rand der Visualisierung in normalisierten Ausgabekoordinaten (0 bis 1). |
| <a id="hdr-color-mapping-mapping-visualization-visualization-bottom"></a>**Visualisierung: Unten** | Unterer Rand der Visualisierung in normalisierten Ausgabekoordinaten (0 bis 1). |
| <a id="hdr-color-mapping-mapping-visualization-visualization-hue-radians"></a>**Visualisierung: Farbton (Radiant)** | Farbtonschnitt der Farbumfangsvisualisierung von -pi bis pi. |
| <a id="hdr-color-mapping-mapping-visualization-visualization-rotation-radians"></a>**Visualisierung: Drehung (Radiant)** | Winkel der Farbumfangsvisualisierung von 0 bis pi/2. |

<a id="hdr-color-mapping-expansion-metadata"></a>

## Erweiterung und Metadaten

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="hdr-color-mapping-expansion-metadata-inverse-tone-mapping"></a>**Inverses Tone Mapping** | Bei **Ein** dürfen geeignete Kurven den Helligkeitsumfang erweitern, wenn der Ausgabebereich größer ist. Bei **Aus** bleibt diese Erweiterung deaktiviert. |
| <a id="hdr-color-mapping-expansion-metadata-gamut-expansion"></a>**Farbumfangserweiterung** | Bei **Ein** darf eine geeignete Methode die Farben in einen größeren Farbumfang strecken. Bei **Aus** bleibt diese Erweiterung deaktiviert. |
| <a id="hdr-color-mapping-expansion-metadata-tone-mapping-metadata"></a>**Metadaten für Tone Mapping** | Standard behält die automatische Auswahl der Metadaten für Dolby Vision und HDR10+ bei. Eine manuelle Auswahl überschreibt sie. |

<a id="hdr-color-mapping-shadows"></a>

## Tiefen

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="hdr-color-mapping-shadows-exposure"></a>**Belichtung** | Passt die Helligkeit in diesem Tonwertbereich in HDR in Blendenstufen an. Positive Werte hellen auf, negative dunkeln ab. |
| <a id="hdr-color-mapping-shadows-contrast"></a>**Kontrast** | Ändert den Kontrast in diesem Tonwertbereich in HDR. 0 ist neutral. |
| <a id="hdr-color-mapping-shadows-saturation"></a>**Sättigung** | Ändert die Farbintensität in diesem Tonwertbereich in HDR. 0 ist neutral. |

<a id="hdr-color-mapping-midtones"></a>

## Mitteltöne

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="hdr-color-mapping-midtones-exposure"></a>**Belichtung** | Passt die Helligkeit in diesem Tonwertbereich in HDR in Blendenstufen an. Positive Werte hellen auf, negative dunkeln ab. |
| <a id="hdr-color-mapping-midtones-contrast"></a>**Kontrast** | Ändert den Kontrast in diesem Tonwertbereich in HDR. 0 ist neutral. |
| <a id="hdr-color-mapping-midtones-saturation"></a>**Sättigung** | Ändert die Farbintensität in diesem Tonwertbereich in HDR. 0 ist neutral. |

<a id="hdr-color-mapping-highlights"></a>

## Lichter

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="hdr-color-mapping-highlights-exposure"></a>**Belichtung** | Passt die Helligkeit in diesem Tonwertbereich in HDR in Blendenstufen an. Positive Werte hellen auf, negative dunkeln ab. |
| <a id="hdr-color-mapping-highlights-contrast"></a>**Kontrast** | Ändert den Kontrast in diesem Tonwertbereich in HDR. 0 ist neutral. |
| <a id="hdr-color-mapping-highlights-saturation"></a>**Sättigung** | Ändert die Farbintensität in diesem Tonwertbereich in HDR. 0 ist neutral. |

<a id="hdr-color-mapping-tone-controls"></a>

## Tonwertregler

Die Regler für [Schatten](#hdr-color-mapping-shadows), [Mitteltöne](#hdr-color-mapping-midtones) und [Lichter](#hdr-color-mapping-highlights) beeinflussen jeweils einen Teil des Helligkeitsbereichs.

---

[← Farbanpassung](DE-Color-Adjustment.md) · [Farbmanagement →](DE-Color-Management.md)
