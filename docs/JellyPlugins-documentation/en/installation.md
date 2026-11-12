# Installation and management

Installing a `.jelly` validates and saves code without running it. Review source, then enable. Actual protected operations request a decision according to access mode and saved rules.

Choose a global mode or per-plugin override. Controls include stop all, disable, JSON settings, individual/all rule removal, source, log, rollback and uninstall. Updating the same ID stops the old instance, clears permissions and requires enabling the new revision. One previous code/data snapshot supports rollback; rollback clears permissions too.

Uninstall can retain local settings/storage; permissions are removed. Schedules require an open client. Unexpected shutdown requires manual enabling. Missing host or unavailable isolation fails with a status message; there is no bypass.
