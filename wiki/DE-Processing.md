[English](Processing.md) · **Deutsch**

[Startseite](DE-Home.md) · [Menü bedienen](DE-Using-the-menu.md) · [Quellen](DE-Sources.md)

**Menüpfad** Verarbeitung. Für den Bildausschnitt siehe [Schwarze Balken und Zoom](DE-Black-Bars-and-Zoom.md).

**Auf dieser Seite**

- [Debanding](#processing-debanding)
- [Adaptives Nachschärfen](#processing-adaptive-sharpen)
- [Deinterlacing](#processing-deinterlacing)
- [Dithering](#processing-dithering)

<a id="processing-debanding"></a>

## Debanding

Bei sichtbaren Stufen im Himmel hilft Debanding. [Dithering](#processing-dithering) hilft bei Stufen durch die Ausgabe.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="processing-debanding-debanding"></a>**Debanding** | Bei **Ein** glättet Debanding sichtbare Stufen in Verläufen, etwa im Himmel. Zu hohe Stärke kann Texturen verwischen. **Aus** überspringt diese Glättung. |
| <a id="processing-debanding-iterations"></a>**Durchläufe** | Mehr Durchläufe entfernen stärkere Farbabstufungen, können aber Details verlieren lassen. |
| <a id="processing-debanding-threshold"></a>**Schwellenwert** | Höher glättet auch deutlichere Stufen. Zu hoch kann gewollte Farbgrenzen oder feine Texturen verwischen. |
| <a id="processing-debanding-radius"></a>**Radius** | Höher bezieht weiter entfernte Bildpunkte ein. Das hilft bei breiten Stufen, kann aber feine Strukturen glätten. |
| <a id="processing-debanding-grain"></a>**Körnung** | Fügt nach der Glättung leichtes Rauschen hinzu, das verbliebene Stufen kaschiert. Je höher, desto sichtbarer die Körnung. |

<a id="processing-adaptive-sharpen"></a>

## Adaptives Nachschärfen

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="processing-adaptive-sharpen-enhance-edges-and-details"></a>**Kanten und Details verstärken** | Bei **Ein** werden Konturen und Details deutlicher. Zu hohe Stärke kann Rauschen und Säume betonen. **Aus** überspringt diesen Schärfungsschritt. |
| <a id="processing-adaptive-sharpen-strength"></a>**Stärke** | Stärke des adaptiven Nachschärfens. 0 deaktiviert den Schärfungseffekt. |

<a id="processing-deinterlacing"></a>

## Deinterlacing

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="processing-deinterlacing-algorithm"></a>**Algorithmus** | Bwdif und Yadif bauen aus Halbbildern vollständige Bilder. Probier Bwdif zuerst, wenn bei alten TV-Aufnahmen Kammkanten auftreten. Aus lässt die Halbbilder unverarbeitet. |
| <a id="processing-deinterlacing-field-order"></a>**Halbbildreihenfolge** | Automatisch folgt den Quellkennzeichnungen. Nur überschreiben, wenn Bewegungen in Material mit Zeilensprung die falsche Halbbildreihenfolge haben. |

<a id="processing-dithering"></a>

## Dithering

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="processing-dithering-algorithm"></a>**Algorithmus** | Blaues Rauschen kaschiert Farbstufen mit einem feinen Muster. Geordnetes Dithering kann ein Raster zeigen. Fehlerdiffusion braucht mehr GPU-Zeit und kann bei großen Bildern auf blaues Rauschen zurückfallen. **Aus** überspringt Dithering. |
| <a id="processing-dithering-change-dither-for-every-frame"></a>**Dithermuster bei jedem Bild ändern** | Bei **Ein** wechselt das Muster je Bild. Das kaschiert ein festes Raster, kann aber flimmern. Bei **Aus** bleibt das Muster fest. Verfügbar bei blauem Rauschen, geordnetem und zufälligem Dithering. |

---

[← Allgemein](DE-General.md) · [Skalierung →](DE-Scaling.md)
