[English](Troubleshooting.md) · **Deutsch**

[Startseite](DE-Home.md) · [Menü bedienen](DE-Using-the-menu.md) · [Fehlerbehebung](DE-Troubleshooting.md)

## Verarbeitung mit Strg+J prüfen

Die Anzeige gliedert sich in Display, Video, Verarbeitung und Leistung. Prüfe Auflösung, Pixelformat, HDR-Metadaten, Profil, verwendete GPU und den Übertragungsweg. Je nach Ausgabe erscheinen das Quellgamma und das Gamma für die Anzeige.

- **CPU memory → GPU upload** bedeutet, dass der Renderer Bilder aus dem Hauptspeicher auf die GPU lädt. **D3D11 texture (GPU direct)** bedeutet, dass der Decoder bzw. die vorgeschaltete Filterkette eine GPU-Textur liefert.
- **RTX Video** zeigt den Zustand von VSR und HDR separat. Bei **Image: native size** passt die Bildgröße bereits. RTX Video HDR ist auch in Originalgröße möglich.
- Die Dolby-Vision- und HDR-Quellangaben beschreiben das Eingangssignal. Prüfe für die tatsächliche Ausgabe den Ausgabepfad, Windows HDR und die Anzeige des Fernsehers.

## Renderzeit und Bildaussetzer

**Render** zeigt die durchschnittliche Renderzeit der letzten Bilder. **Present** und **Queue** beschreiben die Übergabe an die Anzeige und wartende Bilder. Bei Smooth Motion oder einer Quelle mit höherer Bildrate als der Anzeige gilt ein Bildwiederholintervall als Zeitbudget.

Bei **120 fps auf 60 Hz** zeigt FrankyRenderer ohne Smooth Motion ein Bild pro Bildschirmaktualisierung. Strg+J zeigt die tatsächlich dargestellte Bildrate. Die zur Anpassung ausgelassenen Bilder zählen bei den Drops nicht mit. Setze die Statistik zurück und beobachte, ob Drops weiter zunehmen.

**Clock** zeigt die Abweichung der Audiouhr und das geschätzte Intervall zwischen dadurch verursachten Wiederholungen oder Auslassungen. Der Wert braucht kurz zum Einpendeln. Nach einem Sprung im Video, Neustart, einer sprunghaften Zeitkorrektur oder einem Wechsel von Smooth Motion beginnt die Messung erneut.

## RTX VSR funktioniert, RTX Video HDR bleibt aus

1. Prüfe RTX-GPU, Treiber und den Schalter für **RTX Video HDR** in der NVIDIA-App.
2. Aktiviere Windows HDR auf der richtigen Anzeige und teste mit einem SDR-Video. Kontrolliere unter **HDR** die Renderer-Option für RTX Video HDR.
3. Lies den genauen Status unter Strg+J ab. Der Treiber kann HDR ablehnen, während VSR weiterläuft. Prüfe einen zweiten Clip, falls die Meldung auf die Auflösung oder das Eingabeformat hinweist.

## Schwarzes Bild, Sprünge und Filter

Prüfe dieselbe Datei, denselben Decoder und die Filterkette bei normalem Start und nach einem Sprung. Wenn das Problem beim Wechsel eines Skripts oder Filters auftritt, gib den genauen Schritt an. Mit einem zweiten Versuch ohne vorgeschalteten Filter kannst du die betroffene Verarbeitung eingrenzen.

## Diagnosebericht speichern

Aktiviere unter **Allgemein > Diagnose** das **Diagnoseprotokoll**. Starte den Player neu, reproduziere das Problem und wähle **Bericht speichern**. Die Protokollierung gilt für einen Playerstart. Hänge den Bericht nach Prüfung privater Pfade an das GitHub-Issue an. Gib außerdem Build-ID, Player-Version, Decoder, GPU und die Schritte zum Nachstellen an. [Fehler melden](https://github.com/mryx007/FrankyRenderer/issues)
