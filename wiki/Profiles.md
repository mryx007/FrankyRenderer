**English** · [Deutsch](DE-Profiles.md)

[Home](Home.md) · [Using the menu](Using-the-menu.md) · [Troubleshooting](Troubleshooting.md)

## Default settings and display selection

Default is the normal settings baseline for the chosen display. Select the display on the Profiles page; selecting it does not move the player to that screen. Rename changes its friendly name, not its physical identity.

With **Same Settings on All Displays** enabled, the selected display's baseline is copied to the other displays and subsequent baseline changes remain linked. Turn it off to edit displays separately again. Menu themes and language are preferences for the menu itself.

## Create a folder profile

1. Open Profiles and click **+**. Give the profile a descriptive name.
2. Add one or more folders. Their subfolders are included. If folder rules overlap, the closest matching folder rule wins; display restrictions can limit where a profile applies.
3. In the header next to Quality, select the profile you want to edit.
4. Change the settings that should differ from Default. Enable the relevant overrides; values without overrides continue to inherit Default.
5. Play a file from a matching folder and check **Profile** in Ctrl+J.

Files without a matching rule use Default. A profile needs a folder rule to apply automatically. For an anime folder, you can override its upscaler or shaders and inherit HDR settings from Default. Use a display restriction if a costly shader should apply on only one screen.

## Editing and backups

A profile remembers its overrides, folder rules and applicable displays. When Default changes, inherited settings follow it. Check the profile selector before changing a setting so you edit the intended target.

Cancel restores profile settings, names, folders, display restrictions and profiles added or removed during that menu session. Accept changes with OK. External shader, LUT and ICC files are not embedded into a settings backup.

## Settings backup and restore

FrankyRenderer creates **%APPDATA%\FrankyRenderer\Settings.ini** automatically on the first settings change. The file includes display settings, folder profiles and Custom quality settings. On **Profiles**, use **Open Folder** to find it, **Export** to save a copy and **Import** to replace the current settings with a saved copy.

The package also includes **Backup Settings.cmd** and **Import Settings.cmd**. They write or read Settings.ini in the installation folder. Export a backup of the current setup before importing another one. Keep your external ICC profiles, LUTs and custom shaders with the backup.

The compiled shader cache is stored separately under **%LOCALAPPDATA%\FrankyRenderer\Cache**. The uninstaller deletes it when you choose to remove settings.
