[English](Using-the-menu.md) · **Deutsch**

[Startseite](DE-Home.md) · [Menü bedienen](DE-Using-the-menu.md) · [Fehlerbehebung](DE-Troubleshooting.md)

<a id="using-the-menu-examples"></a>

## Typische Situationen

Beispiele aus dem Alltag.

| Situation | Einstellung | Worauf achten? |
| --- | --- | --- |
| Du suchst eine Einstellung, weißt aber nicht, wo sie steht. | Strg+F drücken und ein Wort tippen, etwa **Balken** oder **Gamma**. | Leere die Suche, um zurück zur Seite zu kommen. |
| Du hast zu viel verstellt und das Bild sieht seltsam aus. | Runder Pfeil neben der Einstellung oder **Seite zurücksetzen**. | **Alles zurücksetzen** betrifft die ganze Anzeige. |
| Du willst nichts selbst einstellen, aber mehr Qualität. | Neben **Qualität** auf **Hohe Qualität** wechseln. | Ruckelt es, zurück auf **Standard**. |
| Du willst eine Änderung erst ausprobieren. | Einstellung ändern, im laufenden Video prüfen, dann **OK** oder **Abbrechen**. | **Abbrechen** stellt den vorherigen Stand wieder her. |

## Einstellungen finden und ändern

Die Seitenleiste führt zu **Allgemein, Verarbeitung, Skalierung, Farbe, HDR, Shader, Profile und Tastenkürzel**. Schwarze Balken und Zoom stehen unter **Verarbeitung**. Unter **Farbe** findest du Bildregler und Kalibrierung. Die Regler für Schatten, Mitteltöne und Lichter stehen unter **HDR**.

- **Suche.** Strg+F setzt den Fokus ins Suchfeld. Leere die Suche, um zur normalen Seitenansicht zurückzukehren.
- **Zahlenfelder.** Tippe einen Wert ein oder benutze Minus und Plus. Enter, Tab oder das Verlassen des Felds übernimmt den Wert. Escape stellt den vorherigen Wert wieder her. Der zulässige Bereich steht neben der Einstellung.
- **Zurücksetzen.** Der runde Pfeil setzt einen Wert zurück. Die Seitenaktion betrifft die aktuelle Seite. Beim Standardprofil betrifft das vollständige Zurücksetzen die gewählte Anzeige. In einem Inhaltsprofil entfernt es dessen Überschreibungen. Beim Shader betrifft es dessen Parameter.
- **Qualität.** Probiere Schnell, Standard oder Hohe Qualität. Die PC-Messung berücksichtigt die GPU, aktive Shader, adaptive Schärfung und Smooth Motion. Nach Änderungen zeigt die Auswahl Benutzerdefiniert. Eigene Werte bleiben beim Ausprobieren der Vorgaben abrufbar.
- **Profile.** Die Auswahl neben Qualität legt fest, welches [Profil](DE-Profiles.md) du bearbeitest. Das Standardprofil gehört zur ausgewählten Anzeige. Ordnerprofile übernehmen Werte, die sie selbst nicht überschreiben.
- **Darstellung.** Unter **Allgemein > Darstellung** stellst du Thema, Akzentfarbe und Sprache ein. **Letzte Seite merken** stellt beim Öffnen auch Registerkarte und Scrollposition wieder her.
- **Live-Vorschau.** Der Player bleibt bedienbar. OK übernimmt die Änderungen. Abbrechen stellt auch bearbeitete Profile wieder her. Beim Öffnen einer anderen Videodatei oder Schließen des Players schließt sich das Menü.
- **Fenstergröße.** Du kannst das Menü vergrößern oder verkleinern. FrankyRenderer merkt sich die Größe.
- **Sicherung.** Import und Export stehen auf der Seite [Profile](DE-Profiles.md#einstellungen-sichern-und-wiederherstellen). Die INI sichert Anzeigeeinstellungen, Inhaltsprofile und benutzerdefinierte Qualitätseinstellungen.
- **Ausgegraute Optionen.** Manche Einstellungen hängen von einer anderen Schaltfläche, einem Filter oder dem Verarbeitungsweg ab. Wähle bei eigenen Filtern zuerst die Rolle.

## Menü im Überblick

| Seitenleiste | Register und Themen |
| --- | --- |
| Allgemein | Ausgabe, Bildanalyse, Anzeige, Diagnose, Hintergrund und Darstellung |
| Verarbeitung | Bildbereinigung, Deinterlacing, Dithering sowie schwarze Balken und Zoom |
| Skalierung | Fertige Filter und eigene Filter nach Rolle |
| Farbe | Bildregler und Kalibrierung |
| HDR | Ausgabe, Spitzenerkennung, Tone Mapping, Gamut Mapping und Tonwertregler |
| Shader | Shaderliste, Reihenfolge und Einstellungen je Shader |
| Profile | Anzeigen, Ordnerregeln und Überschreibungen |
| Tastenkürzel | Tasten für Einstellungen und Renderer-Aktionen |

## Begriffe

**Luminanz** bezeichnet Helligkeit und wird bei HDR in Nits angegeben. **Chroma** trägt die Farbinformation. **Farbraum** bezeichnet den darstellbaren Farbumfang. Tone Mapping passt den Helligkeitsumfang an, Gamut Mapping den Farbumfang. **Ringing** zeigt sich als Saum an Kanten. **Banding** sind sichtbare Stufen in Verläufen. Debanding glättet vorhandene Stufen, Dithering kaschiert Rundungsstufen bei der Ausgabe.
