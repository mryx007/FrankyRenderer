[English](Color-Management.md) · **Deutsch**

[Startseite](DE-Home.md) · [Menü bedienen](DE-Using-the-menu.md) · [Quellen](DE-Sources.md)

**Menüpfad** Farbe > Kalibrierung. Hier passt du die Farben an deinen Bildschirm an.

**Auf dieser Seite**

- [Typische Situationen](#color-management-examples)
- [ICC-Profil](#color-management-icc-profile)
- [3D-LUT](#color-management-3d-lut)
- [Bildschirm](#color-management-display)

<a id="color-management-examples"></a>

## Typische Situationen

Beispiele aus dem Alltag. Ändere jeweils nur eine Einstellung und vergleiche dieselbe Szene.

| Situation | Einstellung | Worauf achten? |
| --- | --- | --- |
| Du hast deinen Monitor mit einem Messgerät kalibriert, etwa mit DisplayCAL. | [ICC-Profildatei](#color-management-icc-profile-icc-profile-file) wählen und [ICC-Profil aktivieren](#color-management-icc-profile-enable-icc-profile) **Ein**. | Dieselbe Korrektur nicht zusätzlich per 3D-LUT oder im Grafiktreiber. |
| Monitor mit großem Farbraum: Gesichter wirken rot, Rasen leuchtet neongrün. | [Display-Farbraum](#color-management-display-display-gamut) passend einstellen, etwa **P3-D65**. | Nur im nativen Modus des Monitors, nicht im sRGB-Modus. |
| Der Fernseher ist für den dunklen Raum auf Gamma 2.4 eingestellt. | [Display-Gamma](#color-management-display-display-gamma) **2.40**. | Der Wert muss zur Einstellung des Fernsehers passen. |
| OLED-Fernseher mit Display-Gamma **BT.1886**. | [Display-Kontrast](#color-management-display-display-contrast) **0**. | 0 steht für unendlichen Kontrast. |
| Der Monitor arbeitet mit Gamma 2.2, Filme sollen wie auf einem 2.4-Bildschirm wirken. | [Display-Gamma](#color-management-display-display-gamma) **2.20**, [SDR-Gamma-Verarbeitung](#color-management-display-sdr-gamma-processing) **2.40**. | Dunkle Szenen werden etwas satter. |
| HDR-Filme auf einem SDR-Monitor sollen im dunklen Raum kräftiger wirken. | [HDR-zu-SDR-Gamma-Verarbeitung](#color-management-display-hdr-to-sdr-gamma-processing) **2.40**, oder **Wie SDR**, um den SDR-Wert zu übernehmen. | Derselbe Wert wie Display-Gamma ändert nichts. |
| Ein Film soll mit einer eigenen Farb-LUT laufen. | [LUT aus dem Videoordner](#color-management-3d-lut-lut-from-video-folder) **Ein**, **Film.cube** neben **Film.mkv** legen. | [3D-LUT aktivieren](#color-management-3d-lut-enable-3d-lut) muss eingeschaltet sein. |

<a id="color-management-icc-profile"></a>

## ICC-Profil

Ein ICC-Profil beschreibt die Farbwiedergabe des Displays. Wenn mehrere aktive Korrekturen dieselbe Abweichung beheben, kann die Wirkung zu stark ausfallen.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="color-management-icc-profile-icc-profile-file"></a>**ICC-Profildatei** | Vor dem Aktivieren der ICC-Korrektur das ICC- oder ICM-Farbprofil des Bildschirms auswählen. |
| <a id="color-management-icc-profile-enable-icc-profile"></a>**ICC-Profil aktivieren** | Bei **Ein** korrigiert ein gültiges ICC-Profil die Farben bei SDR-Ausgabe. Bei **Aus** läuft die Ausgabe ohne diese ICC-Korrektur. |
| <a id="color-management-icc-profile-icc-rendering-intent"></a>**ICC-Wiedergabeabsicht** | Legt fest, wie das ICC-Profil Farben in den Farbumfang des Bildschirms überführt. |

<a id="color-management-3d-lut"></a>

## 3D-LUT

Eine 3D-LUT ersetzt Eingabefarben durch andere Farben. Bei **LUT aus dem Videoordner** passt etwa **Film.cube** zu **Film.mkv**.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="color-management-3d-lut-lut-file"></a>**LUT-Datei** | Vor dem Aktivieren der LUT-Verarbeitung eine 3D-LUT-Datei im Format .cube auswählen. |
| <a id="color-management-3d-lut-enable-3d-lut"></a>**3D-LUT aktivieren** | Bei **Ein** wirkt die 3D-LUT an der Stelle, die der LUT-Typ festlegt. Bei **Aus** bleibt die 3D-LUT wirkungslos. |
| <a id="color-management-3d-lut-lut-type"></a>**LUT-Typ** | Standard behält die SDR-Kalibrier-LUT. Normalisiert nutzt lineares RGB; Konvertierung ersetzt Farbumrechnung, Tone Mapping und ICC. Muss zur LUT passen. |
| <a id="color-management-3d-lut-lut-from-video-folder"></a>**LUT aus dem Videoordner** | Bei **Ein** sucht FrankyRenderer neben dem Video zuerst eine gleichnamige .cube-Datei, sonst eine einzelne .cube-Datei im Ordner. Bei **Aus** oder ohne eindeutigen Treffer gilt die gewählte LUT-Datei. **3D-LUT aktivieren** muss eingeschaltet sein. |

<a id="color-management-display"></a>

## Bildschirm

Unter **Strg+J** siehst du das Quellgamma und, soweit verfügbar, das Ausgabegamma.

| Einstellung | Was sie bewirkt |
| --- | --- |
| <a id="color-management-display-display-gamut"></a>**Display-Farbraum** | Für einen Bildschirm mit großem Farbraum im nativen Modus, z. B. P3-D65. Verhindert übersättigte Farben. |
| <a id="color-management-display-display-gamma"></a>**Display-Gamma** | Wie der Bildschirm das Signal dekodiert, z. B. 2.4 bei vielen TVs. Wird genutzt, wenn HDR auf SDR umgerechnet wird, und von der SDR-Gamma-Verarbeitung. |
| <a id="color-management-display-display-contrast"></a>**Display-Kontrast** | Für BT.1886: der gemessene Kontrast des Bildschirms, z. B. 1000 für 1000:1. 0 bedeutet unendlich (OLED), dann entspricht BT.1886 genau 2.40. |
| <a id="color-management-display-sdr-gamma-processing"></a>**SDR-Gamma-Verarbeitung** | Aus lässt SDR unverändert. Sonst sieht SDR auf dem Bildschirm wie diese Gamma aus. |
| <a id="color-management-display-hdr-to-sdr-gamma-processing"></a>**HDR-zu-SDR-Gamma-Verarbeitung** | Dasselbe für HDR auf einem SDR-Bildschirm: Das tongemappte Bild sieht wie die gewählte Gamma aus. **Aus** lässt es wie tongemappt, **Wie SDR** folgt der SDR-Gamma-Verarbeitung. |

---

[← HDR und Farbzuordnung](DE-HDR-and-Color-Mapping.md) · [Shader →](DE-Shaders.md)
