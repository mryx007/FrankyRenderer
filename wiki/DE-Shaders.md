[English](Shaders.md) · **Deutsch**

[Startseite](DE-Home.md) · [Menü bedienen](DE-Using-the-menu.md) · [Quellen](DE-Sources.md)

Mitgelieferte und eigene Shader sowie ihre Reihenfolge.

**Auf dieser Seite**

- [Typische Situationen](#shaders-examples)
- [Shaderkette](#shaders-chain-controls)
- [AMD CAS](#shaders-amd-cas)
- [FineDehalo](#shaders-finedehalo)
- [Unterdrückung von Ringing](#shaders-anti-ringing)
- [PixelClipper](#shaders-pixelclipper)
- [KrigBilateral](#shaders-krigbilateral)
- [CfL Prediction](#shaders-cfl-prediction)
- [NLMeans](#shaders-nlmeans)
- [Bilateral Denoise](#shaders-bilateral-denoise)
- [Chroma Denoise](#shaders-chroma-denoise)
- [hdeband](#shaders-hdeband)
- [neo-f3kdb](#shaders-neo-f3kdb)
- [Guided Detail Enhance](#shaders-guided-detail-enhance)
- [Clarity](#shaders-clarity)
- [FSRCNNX 2x](#shaders-fsrcnnx-2x)
- [AiUpscale Photo 2x](#shaders-aiupscale-photo-2x)
- [RAVU Zoom AR R2](#shaders-ravu-zoom-ar-r2)
- [Anime4K CNN 2x M](#shaders-anime4k-cnn-2x-m)
- [ArtCNN C4F16](#shaders-artcnn-c4f16)
- [CuNNy 2x12 DS](#shaders-cunny-2x12-ds)
- [SSimDownscaler](#shaders-ssimdownscaler)
- [SSimDownscaler_AR](#shaders-ssimdownscaler-ar)
- [DPID](#shaders-dpid)
- [RAVU Zoom R3](#shaders-ravu-zoom-r3)
- [RAVU Zoom AR R3](#shaders-ravu-zoom-ar-r3)
- [Shaderliste und eigene Dateien](#shaders-shader-list-and-custom-files)

<a id="shaders-examples"></a>

## Typische Situationen

Beispiele aus dem Alltag. Ändere jeweils nur eine Einstellung und vergleiche dieselbe Szene.

| Situation | Einstellung | Worauf achten? |
| --- | --- | --- |
| Alte Anime-DVD auf einem großen Bildschirm. | [Anime4K CNN 2x M](#shaders-anime4k-cnn-2x-m) oder [ArtCNN C4F16](#shaders-artcnn-c4f16). | Linien sollen klar sein. Prüfe die Renderzeit mit Strg+J. |
| Alte Realfilm-DVD oder 720p-Datei auf einem 4K-Fernseher. | [FSRCNNX 2x](#shaders-fsrcnnx-2x) oder [RAVU Zoom AR R2](#shaders-ravu-zoom-ar-r2). | Haare und Stoffe sollen feiner wirken, ohne künstliche Säume. |
| Handyvideo vom Abend rauscht stark. | [NLMeans](#shaders-nlmeans). | Feine Texturen und Filmkorn sollen erhalten bleiben. |
| In dunklen Flächen tanzen bunte Flecken. | [Chroma Denoise](#shaders-chroma-denoise). | Kleine farbige Details dürfen nicht verwischen. |
| Streaming-Datei mit Stufen in dunklen Verläufen. | [hdeband](#shaders-hdeband) oder [neo-f3kdb](#shaders-neo-f3kdb). | Alternativ zuerst [Debanding](DE-Processing.md#processing-debanding) unter Verarbeitung testen. |
| Alte Filmabtastung mit hellen Rändern um dunkle Objekte. | [FineDehalo](#shaders-finedehalo). | Gewollte Konturen sollen erhalten bleiben. |
| Das Bild wirkt leicht weich. | [AMD CAS](#shaders-amd-cas) mit geringer Stärke. | Zu viel betont Rauschen und Säume. |
| Das Bild wirkt flau und dunstig. | [Clarity](#shaders-clarity). | Zu viel lässt Gesichter hart wirken. |
| 4K-Video auf einem 1080p-Laptop. | [SSimDownscaler](#shaders-ssimdownscaler) mit **mitchell** als Verkleinerungsfilter. | Feine Muster sollen beim Schwenk nicht flimmern. |
| Rote Schrift oder rote Kleidung hat unscharfe Farbränder. | [KrigBilateral](#shaders-krigbilateral) oder [CfL Prediction](#shaders-cfl-prediction). | Farbkanten sollen sauber an den Konturen liegen. |
| Du willst schnell vergleichen, was die Shader bringen. | [Alle Shader deaktivieren](#shaders-chain-controls-disable-all-shaders), am besten per [Tastenkürzel](DE-Shortcuts.md). | Reihenfolge und Werte bleiben gespeichert. |

<a id="shaders-chain-controls"></a>

## Shaderkette

Die Verarbeitungsstufe legt fest, wann ein Shader greift. Manche wirken nur beim Hoch- oder Herunterskalieren. Vergleiche einzelne Effekte zuerst getrennt.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-chain-controls-disable-all-shaders"></a>**Alle Shader deaktivieren** | Schaltet die Shaderkette aus. Parameterwerte und Reihenfolge bleiben gespeichert. |

<a id="shaders-chain-controls-automatic-order"></a>

### Automatische Reihenfolge

Bei **Ein** sortiert FrankyRenderer geeignete Shader innerhalb ihrer Verarbeitungsstufe. Bei **Aus** kannst du die Position aktiver Shader manuell festlegen.

<a id="shaders-amd-cas"></a>

## AMD CAS

<a id="shaders-amd-cas-enabled"></a>

Kontrastadaptive Schärfung. Vergleiche feine Texturen und harte Konturen.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-amd-cas-strength-str"></a>**Stärke (STR)** | Mehr Stärke macht Konturen und Texturen klarer. Zu viel betont Rauschen und erzeugt Säume. |

<a id="shaders-amd-cas-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-finedehalo"></a>

## FineDehalo

<a id="shaders-finedehalo-enabled"></a>

Reduziert Säume an Kanten. Prüfe helle Ränder an dunklen Objekten.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-finedehalo-horizontal-radius-rx"></a>**Horizontaler Radius (RX)** | Höher erfasst breitere Säume links und rechts von Kanten. Feine Details können weicher werden. |
| <a id="shaders-finedehalo-vertical-radius-ry"></a>**Vertikaler Radius (RY)** | Höher erfasst breitere Säume über und unter Kanten. Feine Details können weicher werden. |
| <a id="shaders-finedehalo-strong-edge-low-thmi"></a>**Starke Kanten: Untere Schwelle (THMI)** | Legt fest, ab welchem Kontrast eine Kante in die Maske für starke Kanten eingeht. |
| <a id="shaders-finedehalo-strong-edge-high-thma"></a>**Starke Kanten: Obere Schwelle (THMA)** | Legt fest, ab welchem Kontrast die Maske eine Kante vollständig als stark behandelt. |
| <a id="shaders-finedehalo-light-edge-low-thlimi"></a>**Helle Kanten: Untere Schwelle (THLIMI)** | Setzt die untere Grenze für die Maske, die hellere Kanten beim Entsäumen schützt. |
| <a id="shaders-finedehalo-light-edge-high-thlima"></a>**Helle Kanten: Obere Schwelle (THLIMA)** | Setzt die obere Grenze für diese Maske. Zusammen mit der unteren Grenze bestimmt sie den Übergang. |
| <a id="shaders-finedehalo-dark-halo-strength-str-d"></a>**Stärke dunkler Säume (STR_D)** | Entfernt dunkle Säume neben Kanten. Zu viel flacht gewollte Konturen ab. |
| <a id="shaders-finedehalo-bright-halo-strength-str-b"></a>**Stärke heller Säume (STR_B)** | Entfernt helle Säume, etwa um weiße Schrift oder nach starkem Nachschärfen. |
| <a id="shaders-finedehalo-low-sensitivity-sens-l"></a>**Untere Empfindlichkeit (SENS_L)** | Ändert am unteren Ende der Maskenschwelle, welche Bildstellen korrigiert werden. |
| <a id="shaders-finedehalo-high-sensitivity-sens-h"></a>**Obere Empfindlichkeit (SENS_H)** | Ändert am oberen Ende der Maskenschwelle, welche Bildstellen korrigiert werden. |
| <a id="shaders-finedehalo-debug-view-debug"></a>**Diagnoseansicht (DEBUG)** | Zeigt statt des normalen Bildes die Kantenerkennung oder eine Korrekturmaske. 0 zeigt wieder das Bild. |

<a id="shaders-finedehalo-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-anti-ringing"></a>

## Unterdrückung von Ringing

<a id="shaders-anti-ringing-enabled"></a>

Dämpft Überschwingen an Kanten nach der Skalierung.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-anti-ringing-sample-taps-tap"></a>**Abtastpunkte (TAP)** | Bestimmt, wie viele Nachbarpixel der Shader zur Erkennung von Säumen prüft. Mehr kostet GPU-Zeit. |
| <a id="shaders-anti-ringing-strength-str"></a>**Stärke (STR)** | Mehr Stärke unterdrückt auffällige Säume. Zu viel kann die Kanten weicher wirken lassen. |

<a id="shaders-anti-ringing-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-pixelclipper"></a>

## PixelClipper

<a id="shaders-pixelclipper-enabled"></a>

Begrenzt lokal auffällige Pixelabweichungen. Prüfe feine Kanten und Spitzlichter.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-pixelclipper-image-sampling-mode-taps-scale"></a>**Abtastmodus für das Bild (TAPS_scale)** | Bei **Aus** prüft PixelClipper vier Abtastpunkte für Bildkanten. Bei **Ein** prüft er zwölf. Das erfasst eine größere Umgebung und braucht mehr GPU-Zeit. |
| <a id="shaders-pixelclipper-chroma-sampling-mode-taps-cscale"></a>**Abtastmodus für Chroma (TAPS_cscale)** | Bei **Aus** prüft PixelClipper vier Abtastpunkte für Farbkanten. Bei **Ein** prüft er zwölf. Das kostet mehr GPU-Zeit. |
| <a id="shaders-pixelclipper-upscaling-limit-clip-scale"></a>**Begrenzung beim Hochskalieren (CLIP_scale)** | Begrenzt Ausreißer beim Vergrößern. Stärkere Begrenzung kann Säume entfernen und feine Konturen glätten. |
| <a id="shaders-pixelclipper-chroma-limit-clip-cscale"></a>**Chromabegrenzung (CLIP_cscale)** | Begrenzt Farbsäume beim Vergrößern der Farbebene. Zu viel kann farbige Kanten weicher machen. |
| <a id="shaders-pixelclipper-downscaling-limit-clip-dscale"></a>**Begrenzung beim Herunterskalieren (CLIP_dscale)** | Begrenzt Ausreißer an Kanten beim Verkleinern. Zu viel schwächt feine Konturen ab. |

<a id="shaders-pixelclipper-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-krigbilateral"></a>

## KrigBilateral

<a id="shaders-krigbilateral-enabled"></a>

Kantenorientierte Rekonstruktion der Farbauflösung.

<a id="shaders-krigbilateral-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-cfl-prediction"></a>

## CfL Prediction

<a id="shaders-cfl-prediction-enabled"></a>

Nutzt Helligkeitsinformationen, um Farbauflösung an Kanten zu rekonstruieren.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-cfl-prediction-chroma-offset-x-chroma-offset-x"></a>**Chromaversatz X (chroma_offset_x)** | Verschiebt die Referenz für die Farbrekonstruktion seitlich. Hilft bei seitlich versetzten Farbkanten. |
| <a id="shaders-cfl-prediction-chroma-offset-y-chroma-offset-y"></a>**Chromaversatz Y (chroma_offset_y)** | Verschiebt die Referenz für die Farbrekonstruktion nach oben oder unten. Hilft bei vertikal versetzten Farbkanten. |
| <a id="shaders-cfl-prediction-distance-coeff-distance-coeff"></a>**Abstandskoeffizient (distance_coeff)** | Höher gewichtet nahe Pixel stärker. Niedriger lässt weiter entfernte Pixel mehr mitbestimmen. |
| <a id="shaders-cfl-prediction-intensity-coeff-intensity-coeff"></a>**Intensitätskoeffizient (intensity_coeff)** | Höher gewichtet Pixel mit ähnlicher Helligkeit stärker. Das kann Farbübergänge an Kanten sauberer halten. |

<a id="shaders-cfl-prediction-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-nlmeans"></a>

## NLMeans

<a id="shaders-nlmeans-enabled"></a>

Rauschminderung anhand ähnlicher Bildbereiche. Prüfe, ob Filmkorn und feine Texturen erhalten bleiben.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-nlmeans-reference-mode-ref"></a>**Referenzmodus (REF)** | Bei **Aus** nutzt NLMeans seine Grundmethode. Bei **Ein** rechnet der Rauschfilter mit einer zusätzlichen internen Referenz. |

<a id="shaders-nlmeans-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-bilateral-denoise"></a>

## Bilateral Denoise

<a id="shaders-bilateral-denoise-enabled"></a>

Glättet Rauschen mit Rücksicht auf Helligkeits- oder Farbunterschiede an Kanten.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-bilateral-denoise-sample-taps-tap"></a>**Abtastpunkte (TAP)** | Bestimmt, wie weit der Filter um jedes Pixel sucht. Mehr Abtastpunkte können deutlich mehr GPU-Zeit kosten. |
| <a id="shaders-bilateral-denoise-ss-ss"></a>**SS (SS)** | Höher gibt weiter entfernten Nachbarn mehr Einfluss. Der Rauschfilter wirkt dadurch über eine größere Fläche. |
| <a id="shaders-bilateral-denoise-sr-sr"></a>**SR (SR)** | Höher lässt auch Pixel mit stärkerem Helligkeitsunterschied zusammenfließen. Das kann Rauschen und feine Kanten glätten. |

<a id="shaders-bilateral-denoise-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-chroma-denoise"></a>

## Chroma Denoise

<a id="shaders-chroma-denoise-enabled"></a>

Dämpft Farbrauschen. Prüfe besonders feine Farbübergänge.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-chroma-denoise-threshold-thr"></a>**Schwellenwert (THR)** | Steuert, welche Farbabweichungen als Rauschen behandelt werden. Starke Korrektur kann kleine farbige Details verwischen. |

<a id="shaders-chroma-denoise-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-hdeband"></a>

## hdeband

<a id="shaders-hdeband-enabled"></a>

Glättet Stufen in Farbverläufen. Prüfe dunkle Übergänge und Filmkorn.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-hdeband-s-s"></a>**S (S)** | Höher gibt weiter entfernten Pixeln weniger Einfluss bei der Glättung. |
| <a id="shaders-hdeband-si-si"></a>**SI (SI)** | Höher bevorzugt ähnlich helle Pixel. Niedriger mischt mehr unterschiedliche Helligkeiten. |
| <a id="shaders-hdeband-sr-sr"></a>**SR (SR)** | Höher lässt die Suche eher weiterlaufen, wenn ein Pixel nicht zum bisherigen Verlauf passt. |
| <a id="shaders-hdeband-sw-sw"></a>**SW (SW)** | Höher gewichtet das Originalpixel stärker. Die Nachbarpixel verändern es dann weniger. |
| <a id="shaders-hdeband-radius-radius"></a>**Radius (RADIUS)** | Höher erlaubt mehr Suchschritte nach außen. Das kann breitere Stufen glätten und kostet mehr GPU-Zeit. |
| <a id="shaders-hdeband-sparsity-sparsity"></a>**Abtastlücken (SPARSITY)** | Höher setzt die Abtastpunkte weiter auseinander. Der Filter erreicht damit einen größeren Bereich. |
| <a id="shaders-hdeband-directions-directions"></a>**Richtungen (DIRECTIONS)** | Mehr Richtungen prüfen Verläufe gleichmäßiger, brauchen aber mehr GPU-Zeit. |
| <a id="shaders-hdeband-run-start-run-start"></a>**Startwert der Folge (RUN_START)** | Bei **Ein** startet die Suche mit der Annahme, dass ein gleichmäßiger Verlauf vorliegt. Bei **Aus** prüft der Shader den Verlauf von den ersten Abtastpunkten an. |
| <a id="shaders-hdeband-tolerance-tolerance"></a>**Toleranz (TOLERANCE)** | Höher erkennt auch unruhigere Verläufe als glatt. Zu viel kann Textur mitglätten. |

<a id="shaders-hdeband-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-neo-f3kdb"></a>

## neo-f3kdb

<a id="shaders-neo-f3kdb-enabled"></a>

Alternative zur Reduktion von Banding mit optionalem Korn.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-neo-f3kdb-range-range"></a>**Bereich (RANGE)** | Höher sucht weiter entfernt nach Vergleichspixeln. Das kann breite Stufen glätten, aber auch über echte Kanten hinweg greifen. |
| <a id="shaders-neo-f3kdb-grain-grain"></a>**Körnung (GRAIN)** | Fügt feine künstliche Körnung hinzu, die verbliebene Farbstufen kaschiert. |
| <a id="shaders-neo-f3kdb-grad-grad"></a>**GRAD (GRAD)** | Bei **Ein** passt der Shader seine Schwellen an den örtlichen Farbverlauf an. Bei **Aus** gelten die eingestellten Schwellen direkt. |
| <a id="shaders-neo-f3kdb-average-threshold-thr-avg"></a>**Mittlerer Schwellenwert (THR_AVG)** | Höher glättet auch größere Abweichungen von der Umgebung. Feine Texturen können dabei verloren gehen. |
| <a id="shaders-neo-f3kdb-maximum-threshold-thr-max"></a>**Maximaler Schwellenwert (THR_MAX)** | Begrenzt, wie stark ein einzelner Nachbar abweichen darf, bevor er bei der Glättung ausgeschlossen wird. |
| <a id="shaders-neo-f3kdb-middle-threshold-thr-mid"></a>**Zentraler Schwellenwert (THR_MID)** | Prüft Nachbarn paarweise. Das hilft, echte Kanten von gleichmäßigen Verläufen zu unterscheiden. |
| <a id="shaders-neo-f3kdb-combined-threshold-thr-all"></a>**Kombinierter Schwellenwert (THR_ALL)** | Bei **Ein** übernimmt die Maximum- und Paarprüfung den Durchschnittsschwellenwert. Bei **Aus** gilt für jede Prüfung ihr eigener Schwellenwert. |
| <a id="shaders-neo-f3kdb-ab-ab"></a>**AB (AB)** | Bestimmt bei aktivem GRAD, wie stark passende Farbverläufe die Glättung erleichtern. |
| <a id="shaders-neo-f3kdb-am-am"></a>**AM (AM)** | Bestimmt bei aktivem GRAD, wie ähnlich die Richtungen zweier Verläufe für die Zusatzkorrektur sein müssen. |

<a id="shaders-neo-f3kdb-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-guided-detail-enhance"></a>

## Guided Detail Enhance

<a id="shaders-guided-detail-enhance-enabled"></a>

Verstärkt lokale Details. Prüfe Textur und zusätzliche Säume.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-guided-detail-enhance-radius-rad"></a>**Radius (RAD)** | Höher vergrößert die Umgebung, aus der der Shader Details ableitet. Dadurch werden größere Strukturen erfasst. |
| <a id="shaders-guided-detail-enhance-regularization-eps"></a>**Regularisierung (EPS)** | Höher glättet die Basis stärker. Dadurch ändert sich, welche feinen Strukturen als Details hervorgehoben werden. |
| <a id="shaders-guided-detail-enhance-subsampling-subsam"></a>**Unterabtastung (SUBSAM)** | Höher rechnet mit weniger Abtastpunkten. Das spart GPU-Zeit, kann aber feine Details ungenauer erfassen. |
| <a id="shaders-guided-detail-enhance-strength-str"></a>**Stärke (STR)** | Höher hebt Texturen stärker hervor. Rauschen und Säume können ebenfalls deutlicher werden. |

<a id="shaders-guided-detail-enhance-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-clarity"></a>

## Clarity

<a id="shaders-clarity-enabled"></a>

Verstärkt lokalen Kontrast. Schatten und Lichter haben eigene Stärkeregler.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-clarity-radius-rad"></a>**Radius (RAD)** | Wählt, wie groß die Strukturen sind, deren Kontrast der Shader bearbeitet. |
| <a id="shaders-clarity-shift-shift"></a>**Verschiebung (SHIFT)** | Höher tastet weiter vom Pixel entfernt ab. Der lokale Kontrast wirkt dadurch auf größere Strukturen. |
| <a id="shaders-clarity-strength-str"></a>**Stärke (STR)** | Höher betont Strukturen und Texturen. Zu viel lässt das Bild hart wirken. |
| <a id="shaders-clarity-shadow-strength-strd"></a>**Stärke in den Tiefen (STRD)** | Verstärkt den lokalen Kontrast in Schatten. Zu viel macht dunkles Rauschen sichtbarer. |
| <a id="shaders-clarity-highlight-strength-strl"></a>**Stärke in den Lichtern (STRL)** | Verstärkt den lokalen Kontrast in Lichtern. Zu viel erzeugt Säume oder überzeichnet Texturen. |
| <a id="shaders-clarity-bm-bm"></a>**BM (BM)** | Wählt die Art der Überblendung, etwa Weiches Licht oder Multiplizieren. Die Zahl steht für einen Modus. |
| <a id="shaders-clarity-cd-cd"></a>**CD (CD)** | Bestimmt, in welchen dunklen Tönen die Maske den Clarity-Effekt abschwächt. |
| <a id="shaders-clarity-cl-cl"></a>**CL (CL)** | Bestimmt, in welchen hellen Tönen die Maske den Clarity-Effekt abschwächt. |
| <a id="shaders-clarity-debug-view-debug"></a>**Diagnoseansicht (DEBUG)** | Zeigt die herausgearbeiteten Details oder die Schutzmaske. 0 zeigt das normale Bild. |

<a id="shaders-clarity-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-fsrcnnx-2x"></a>

## FSRCNNX 2x

<a id="shaders-fsrcnnx-2x-enabled"></a>

Neuronaler Hochskalierer für eine zweifache Vergrößerung.

<a id="shaders-fsrcnnx-2x-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-aiupscale-photo-2x"></a>

## AiUpscale Photo 2x

<a id="shaders-aiupscale-photo-2x-enabled"></a>

Zweifache Hochskalierung für fotografisches Material mit optionaler Schärfung.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-aiupscale-photo-2x-sharpen-sharp"></a>**Nachschärfen (SHARP)** | Bei **Ein** schärft der Shader beim Hochskalieren zusätzlich. Schrift oder Haut kann dadurch härter wirken. Bei **Aus** bleibt es bei der normalen Hochskalierung. |

<a id="shaders-aiupscale-photo-2x-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-ravu-zoom-ar-r2"></a>

## RAVU Zoom AR R2

<a id="shaders-ravu-zoom-enabled"></a>

<a id="shaders-ravu-zoom"></a>

Hochskalierung mit Kantenbegrenzung. Prüfe feine Konturen und Halos.

<a id="shaders-ravu-zoom-ar-r2-chain-position"></a>

<a id="shaders-ravu-zoom-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-anime4k-cnn-2x-m"></a>

## Anime4K CNN 2x M

<a id="shaders-anime4k-cnn-2x-m-enabled"></a>

Neuronale Hochskalierung für gezeichnete Inhalte.

<a id="shaders-anime4k-cnn-2x-m-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-artcnn-c4f16"></a>

## ArtCNN C4F16

<a id="shaders-artcnn-c4f16-enabled"></a>

Neuronale Hochskalierung. Prüfe Linien, Farbkanten und GPU-Zeit.

<a id="shaders-artcnn-c4f16-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-cunny-2x12-ds"></a>

## CuNNy 2x12 DS

<a id="shaders-cunny-2x12-ds-enabled"></a>

Neuronaler Hochskalierer. Prüfe feine Linien und GPU-Zeit.

<a id="shaders-cunny-2x12-ds-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-ssimdownscaler"></a>

## SSimDownscaler

<a id="shaders-ssimdownscaler-enabled"></a>

Verfeinert das Verkleinern anhand lokaler Bildstruktur. Mitchell eignet sich als Ausgangspunkt. Überschärfung (THR) ergänzt die Schärfung.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-ssimdownscaler-oversharp-thr"></a><a id="shaders-ssimdownscaler-threshold-thr"></a>**Überschärfung (THR)** | Zusätzliche Schärfe zu SSIM. 0 ist die Vorgabe des Autors. |
| <a id="shaders-ssimdownscaler-debug-view-debug"></a>**Diagnoseansicht (DEBUG)** | Bei **Ein** zeigt die Diagnoseansicht die Korrektur in Rot und Blau. Bei **Aus** erscheint das normal skalierte Video. |

<a id="shaders-ssimdownscaler-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-ssimdownscaler-ar"></a>

## SSimDownscaler_AR

Die mitgelieferte Variante von SSimDownscaler begrenzt Überschwingen an harten Kanten. Ihre Kantenbegrenzung verwendet [Globale Unterdrückung von Ringing](DE-Scaling.md#scaling-filters-global-anti-ringing). **Überschärfung (THR)** regelt die zusätzliche Schärfung. Vergleiche beide Varianten bei gleicher Ausgabegröße.

<a id="shaders-dpid"></a>

## DPID

<a id="shaders-dpid-enabled"></a>

Detailerhaltendes Verkleinern. Prüfe Bewegungen in Bereichen mit regelmäßigem Muster.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="shaders-dpid-lbd-lbd"></a>**LBD (LBD)** | Höher gewichtet auffällige Abweichungen stärker, damit sie beim Verkleinern sichtbar bleiben. |

<a id="shaders-dpid-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-ravu-zoom-r3"></a>

## RAVU Zoom R3

RAVU-Hochskalierung mit Radius 3.

<a id="shaders-ravu-zoom-r3-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-ravu-zoom-ar-r3"></a>

## RAVU Zoom AR R3

RAVU-Hochskalierung mit Radius 3 und Anti-Ringing.

<a id="shaders-ravu-zoom-ar-r3-chain-position"></a>

Die [Reihenfolge](#shaders-chain-controls-automatic-order) legt fest, wann der aktive Shader innerhalb seiner Stufe angewendet wird.

<a id="shaders-shader-list-and-custom-files"></a>

## Shaderliste und eigene Dateien

<a id="eigene-shader"></a>

Schalte Shader direkt in der Liste ein und aus. **Alle aus** hilft beim Vergleich. **Shader zurücksetzen** stellt die Werte des ausgewählten Shaders wieder her. Unter **Strg+J** siehst du, ob ein Shader noch vorbereitet wird.

## Eigene Shader hinzufügen

Lege mpv-kompatible **.glsl**- oder **.hook**-Dateien in **shaders\Custom** neben FrankyRenderer.ax ab. Shader-Pakete mit einer **input.conf** können eigene Einträge je Modus bereitstellen. Entpacke dafür das ganze Paket und erhalte den Verzeichnisaufbau. Der Renderer zeigt die erkannten Parameter an. Die Bedingungen des Shader-Hooks müssen zum Video und zur Verarbeitungsstufe passen.

Die mitgelieferten PlayKit-Shader stehen in **shaders\PlayKit**. Wenn du einen davon aus dem Verzeichnis entfernst, verschwindet er aus der Liste. Ein Update kann mitgelieferte Dateien erneut ablegen.

---

[← Farbmanagement](DE-Color-Management.md) · [Quellen →](DE-Sources.md)
