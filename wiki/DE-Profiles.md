[English](Profiles.md) · **Deutsch**

[Startseite](DE-Home.md) · [Menü bedienen](DE-Using-the-menu.md) · [Fehlerbehebung](DE-Troubleshooting.md)

## Standardprofil und Anzeigen

Wähle auf der Seite **Profile** die Anzeige, deren Standardwerte du bearbeiten willst. Die Wiedergabe bleibt auf ihrem Bildschirm. Du kannst den Anzeigen einen eigenen Namen geben.

**Gleiche Einstellungen für alle Anzeigen** kopiert die Einstellungen der ausgewählten Anzeige auf die anderen Anzeigen und hält die Standardwerte synchron. Schalte die Option aus, um die Anzeigen wieder einzeln anzupassen.

## Ordnerprofil anlegen

1. Öffne **Profile** und klicke auf **+**. Gib dem Profil einen Namen.
2. Füge einen oder mehrere Ordner hinzu. Unterordner zählen mit. Bei überlappenden Regeln gewinnt der näher am Video liegende Ordner. Über die Anzeigenliste kannst du die Regel auf bestimmte Bildschirme begrenzen.
3. Wähle im Kopfbereich neben **Qualität** das Profil aus, das du bearbeiten willst.
4. Aktiviere die gewünschten Überschreibungen. Alle übrigen Werte stammen aus dem Standardprofil.
5. Spiele eine Datei aus dem Ordner ab und prüfe unter **Strg+J**, welches Profil angewendet wird.

Eine Datei ohne passende Regel verwendet das Standardprofil. Ein Ordnerprofil ohne Ordnerregel wird nicht automatisch angewendet. Für einen Anime-Ordner kannst du beispielsweise einen anderen Hochskalierer oder bestimmte Shader auswählen und die HDR-Ausgabe vom Standardprofil übernehmen.

## Profile bearbeiten und sichern

Prüfe vor Änderungen das Profil neben **Qualität**. Vererbte Werte folgen dem Standardprofil.

**Abbrechen** stellt die Profilnamen, Ordner, Anzeigen und Werte vor der Bearbeitung wieder her. Es stellt auch gelöschte Profile wieder her und verwirft in der Sitzung neu angelegte Profile. **OK** übernimmt die Änderungen.

## Einstellungen sichern und wiederherstellen

FrankyRenderer legt **%APPDATA%\FrankyRenderer\Settings.ini** bei der ersten Änderung an. Darin stehen die Anzeigeeinstellungen, Inhaltsprofile und benutzerdefinierten Qualitätseinstellungen. Auf **Profile** öffnet **Ordner öffnen** das Verzeichnis. **Exportieren** speichert eine Kopie. **Importieren** ersetzt die aktuellen Einstellungen durch die Kopie.

**Backup Settings.cmd** sichert die Datei als Settings.ini im Installationsordner. **Import Settings.cmd** liest sie von dort ein. Sichere vor einem Import den aktuellen Stand. Bewahre externe ICC-Profile, LUTs und eigene Shader separat auf.

Die kompilierten Shader liegen unter **%LOCALAPPDATA%\FrankyRenderer\Cache**. Bei der Deinstallation wird dieser Cache zusammen mit den Einstellungen entfernt, wenn du das Löschen der Einstellungen auswählst.
