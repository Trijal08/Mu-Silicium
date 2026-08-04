#!/usr/bin/env bash
#
# Builds a FAT32 EFI System Partition Image containing WinPE, ready to be Written
# to a Partition on the Device.
#
# The WinPE Payload itself cannot be Produced here. Run "copype arm64 <dir>" on a
# Windows Machine with the Assessment and Deployment Kit plus its WinPE Add On,
# and Point this Script at the "media" Directory it Leaves behind. That Directory
# holds the Boot Manager, the Boot Configuration Data, the Ram Disk Descriptor and
# the Boot Image, all of which are Microsoft Binaries.
#
# Use a 23H2 or Older Build. Newer ones are not Known to Boot on any Device in
# this Tree.
#
# WinPE is Deliberately the Target rather than a Full Installation. Its Boot
# Configuration Loads boot.wim into a Ram Disk, so once the Boot Manager has Read
# that File through the Firmware's own Block Interface, Windows never asks for a
# Native Storage Driver. No such Driver Exists for this Controller.
#
set -euo pipefail

MEDIA="${1:-}"
OUTPUT="${2:-winpe-esp.img}"
SIZE_MB="${3:-1024}"

if [ -z "$MEDIA" ] || [ ! -d "$MEDIA" ]; then
	cat <<-USAGE
	Usage: $0 <copype media directory> [output image] [size in megabytes]

	  <copype media directory>  The "media" Directory Produced by "copype arm64".
	                            Must contain EFI/Boot/bootaa64.efi and
	                            sources/boot.wim.
	  [output image]            Defaults to winpe-esp.img
	  [size in megabytes]       Defaults to 1024. Must Exceed the Payload with
	                            Room to Spare.
	USAGE
	exit 1
fi

#
# Refuse Early if the Payload is not what it should be. A Missing Boot Manager or
# Boot Image only Shows up as a Silent Failure on the Device otherwise.
#
for required in "EFI/Boot/bootaa64.efi" "sources/boot.wim"; do
	if [ ! -f "$MEDIA/$required" ]; then
		echo "Error: $MEDIA/$required is Missing. Is this really a copype media Directory?" >&2
		exit 1
	fi
done

if [ ! -f "$MEDIA/Boot/BCD" ]; then
	echo "Warning: $MEDIA/Boot/BCD is Missing. The Boot Manager will not find its Configuration." >&2
fi

if [ ! -f "$MEDIA/Boot/boot.sdi" ]; then
	echo "Warning: $MEDIA/Boot/boot.sdi is Missing. The Ram Disk cannot be Built without it." >&2
fi

PAYLOAD_KB="$(du -sk "$MEDIA" | cut -f1)"
CAPACITY_KB="$((SIZE_MB * 1024))"

if [ "$PAYLOAD_KB" -ge "$CAPACITY_KB" ]; then
	echo "Error: The Payload is $((PAYLOAD_KB / 1024)) MB but the Image is only ${SIZE_MB} MB." >&2
	exit 1
fi

echo "Creating a ${SIZE_MB} MB Image for a $((PAYLOAD_KB / 1024)) MB Payload."

rm -f "$OUTPUT"
truncate -s "${SIZE_MB}M" "$OUTPUT"

#
# FAT32 Explicitly. The Firmware's File System Driver Handles it, and a Smaller
# Image would otherwise be Formatted as FAT16 and Refused as an EFI System
# Partition.
#
mkfs.vfat -F 32 -n "WINPE" "$OUTPUT" >/dev/null

#
# Copy the Tree in Wholesale. The Names Windows looks for are Case Insensitive on
# FAT, so the Layout is Preserved as it Comes.
#
( cd "$MEDIA" && find . -mindepth 1 -maxdepth 1 -printf '%f\n' ) | while read -r entry; do
	if [ -d "$MEDIA/$entry" ]; then
		mcopy -i "$OUTPUT" -s -Q "$MEDIA/$entry" "::"
	else
		mcopy -i "$OUTPUT" -Q "$MEDIA/$entry" "::"
	fi
done

echo
echo "Wrote $OUTPUT. Contents:"
mdir -i "$OUTPUT" -/ "::" | tail -n 20

cat <<-NEXT

	Write it to the Partition you Created, from Android or Recovery:

	  dd if=$OUTPUT of=/dev/block/by-name/<your partition> bs=4M

	The Partition should carry the EFI System Partition Type so that the Firmware
	Enumerates it, which is GUID C12A7328-F81F-11D2-BA4B-00A0C93EC93B.

	Then Boot the Device. Simple Init can Launch the Boot Manager either through
	its Prober, which Looks for EFI Binaries on every Volume it can See, or
	through a Boot Entry with mode = "efi" and efi_file Pointing at
	EFI/Boot/bootaa64.efi on this Volume.
NEXT
