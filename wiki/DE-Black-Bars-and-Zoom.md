[English](Black-Bars-and-Zoom.md) · **Deutsch**

[Startseite](DE-Home.md) · [Menü bedienen](DE-Using-the-menu.md) · [Quellen](DE-Sources.md)

**Menüpfad** Verarbeitung > Schwarze Balken / Zoom.

Passe den Bildausschnitt an und halte eingebrannte Untertitel sichtbar.

**Auf dieser Seite**

- [Typische Situationen](#black-bars-zoom-examples)
- [Erkennung und Zoom](#black-bars-zoom-detection-zoom)
- [Erkennung bei wechselnden Bildformaten](#black-bars-zoom-detection-and-changing-aspect-ratios)

<a id="black-bars-zoom-examples"></a>

## Typische Situationen

Beispiele aus dem Alltag. Ändere jeweils nur eine Einstellung und vergleiche dieselbe Szene.

| Situation | Einstellung | Worauf achten? |
| --- | --- | --- |
| Kinofilm im Breitbild (2,39:1) auf einem 16:9-Fernseher. | [Zoommodus](#black-bars-zoom-detection-zoom-zoom-mode) **Einpassen**. | Oben und unten bleiben Balken, dafür fehlt nichts vom Bild. |
| Du willst den ganzen Bildschirm ohne Balken füllen. | [Zoommodus](#black-bars-zoom-detection-zoom-zoom-mode) **Ausfüllen**. | Links und rechts geht Bild verloren, etwa Personen am Rand. |
| 21:9-Monitor oder Breitbild-Leinwand: Der Film hat eingebrannte Balken und erscheint klein in der Mitte. | [Im Video enthaltene schwarze Balken erkennen](#black-bars-zoom-detection-zoom-detect-hardcoded-black-bars) und [Schwarze Balken abschneiden](#black-bars-zoom-detection-zoom-crop-black-bars) **Ein**. | Der Film füllt danach die ganze Breite. |
| Untertitel stehen im unteren Balken und verschwinden nach dem Abschneiden. | [Eingebrannte Untertitel schützen](#black-bars-zoom-detection-zoom-protect-burned-in-subtitles) **Ein**. | Separate Untertitelspuren zeigt der Player selbst an. |
| Du willst unten einen großen Balken für Untertitel statt zwei kleiner Balken. | [Vertikale Bildposition](#black-bars-zoom-detection-zoom-vertical-image-position) **Oben**. | Das Bild rückt an den oberen Rand. |
| Dunkle Nachtszenen werden fälschlich abgeschnitten. | [Schwarzschwelle](#black-bars-zoom-detection-zoom-black-threshold) senken, etwa von 2 auf 1. | Sehr dunkle Bildteile sollen sichtbar bleiben. |
| Dunkelgraue Balken werden nicht erkannt. | [Schwarzschwelle](#black-bars-zoom-detection-zoom-black-threshold) erhöhen, etwa auf 4. | Zu hoch hält dunkle Bildteile für Balken. |
| Filme mit wechselndem Format, etwa IMAX-Szenen, springen ständig im Zoom. | [Erkennungsstabilisierung](#black-bars-zoom-detection-zoom-detection-stability-seconds) erhöhen, etwa auf 5 Sekunden. | Länger wirkt ruhiger, reagiert aber später auf einen echten Formatwechsel. |

<a id="black-bars-zoom-detection-zoom"></a>

## Erkennung und Zoom

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="black-bars-zoom-detection-zoom-vertical-image-position"></a>**Vertikale Bildposition** | Richtet das Bild oben oder unten aus. Mit „Eingebrannte Untertitel schützen“ bleiben Untertitel in den Balken sichtbar. |
| <a id="black-bars-zoom-detection-zoom-detect-hardcoded-black-bars"></a>**Im Video enthaltene schwarze Balken erkennen** | Bei **Ein** sucht der Renderer nach Balken im Videobild. Bei **Aus** bleibt die automatische Balkenerkennung aus. |
| <a id="black-bars-zoom-detection-zoom-crop-black-bars"></a>**Schwarze Balken abschneiden** | Bei **Ein** schneidet der Renderer erkannte Balken ab. Bei **Aus** dient die Erkennung nur zur Bestimmung des Bildbereichs. Die automatische Beschneidung braucht die Balkenerkennung. |
| <a id="black-bars-zoom-detection-zoom-protect-burned-in-subtitles"></a>**Eingebrannte Untertitel schützen** | Bei **Ein** kann der Renderer erkannte eingebrannte Untertitel aus ausgeblendeten Balken wieder anzeigen. Bei **Aus** wird ausgeblendeter Text in diesen Balken nicht wieder eingeblendet. |
| <a id="black-bars-zoom-detection-zoom-keep-a-stable-zoom"></a><a id="black-bars-zoom-detection-zoom-zoom-mode"></a>**Zoommodus** | Einpassen erhält den gesamten aktiven Inhalt. Ausfüllen bedeckt die Ausgabefläche und kann Bildinhalte abschneiden. |
| <a id="black-bars-zoom-detection-zoom-black-threshold"></a>**Schwarzschwelle** | Höher erkennt auch dunkelgraue Balken, kann aber dunkle Bildteile für Balken halten. |
| <a id="black-bars-zoom-detection-zoom-detection-stability-seconds"></a>**Erkennungsstabilisierung (Sekunden)** | Länger wartet nach Änderungen des Ausschnitts ab. Kürzer reagiert schneller auf wechselnde Formate. |

<a id="black-bars-zoom-detection-and-changing-aspect-ratios"></a>

## Erkennung bei wechselnden Bildformaten

Die Erkennung prüft Balken im Video selbst. Beim Wechsel vom Logo zum Hauptfilm wartet sie kurz, bevor sie neu zuschneidet. Die frühere Option **Zoom stabil halten** wurde entfernt.

Bei einem Breitbildfilm auf 16:9 behält **Einpassen** das ganze Bild. **Ausfüllen** kann links und rechts etwas abschneiden. Der Untertitelschutz kann erkannten Text am Bildschirmrand wieder einblenden. Separate Untertitelspuren laufen über den Player.

---

[← Skalierung](DE-Scaling.md) · [Farbanpassung →](DE-Color-Adjustment.md)
