[English](Black-Bars-and-Zoom.md) · **Deutsch**

[Startseite](DE-Home.md) · [Menü bedienen](DE-Using-the-menu.md) · [Quellen](DE-Sources.md)

**Menüpfad** Verarbeitung > Schwarze Balken / Zoom.

Passe den Bildausschnitt an und halte eingebrannte Untertitel sichtbar.

**Auf dieser Seite**

- [Erkennung und Zoom](#black-bars-zoom-detection-zoom)
- [Erkennung bei wechselnden Bildformaten](#black-bars-zoom-detection-and-changing-aspect-ratios)

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
