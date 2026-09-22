# recovery/

Recovery environment (spec §51): boot entry "MacLiteOS Recovery" starts
`maclite-recovery`, a menu over the installed volumes:

  1. Verify base image (squashfs fsck + checksum manifest)
  2. Repair user data (ext4 fsck -y on MACLITE_DATA)
  3. Reinstall base (keeps user data)
  4. Erase everything (needs typed YES twice)
  5. Shell

It is deliberately text-mode: recovery must work when the compositor cannot.
