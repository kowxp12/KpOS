# KPFS v1 — File System Specification

## 1. General Rules

### 1. KPFS is the native file system designed for KpOS.
### 2. KPFS must support both virtual disk images and physical storage devices.
### 3. KPFS must not impose a predefined maximum disk capacity.
### 4. KPFS must detect the actual capacity of the storage device during initialization or formatting.
### 5. The detected disk capacity must be stored in the Superblock.
### 6. KPFS must support disks of different capacities without requiring changes to its core implementation.
### 7. All storage operations must use a standard 512-byte sector size.
### 8. KPFS must use 64-bit unsigned integers for sector addresses and disk capacity calculations.
### 9. KPFS must be independent of the underlying storage hardware.

## 2. Sector Rules

### 1. Each sector must contain exactly 512 bytes.
### 2. All disk read and write operations must operate on complete sectors.
### 3. Files must occupy whole sectors.
### 4. Unused bytes at the end of an allocated file must not be treated as file content.
### 5. The actual number of available sectors must be detected from the storage device.
### 6. All sector calculations must prevent integer overflow and invalid disk access.

## 3. Disk Layout

The disk layout must be dynamically determined during formatting.

Region| Purpose
Superblock| Stores file system metadata
File Table| Stores file entries
File Data| Stores actual file contents

### 1. The Superblock may occupy multiple consecutive sectors.
### 2. The Superblock size must be determined during formatting.
### 3. The File Table must support dynamic expansion.
### 4. File Data must use the remaining available sectors.
### 5. No fixed disk size or fixed number of data sectors must be assumed.
### 6. Every disk region must have a clearly defined starting sector and size.
### 7. Disk regions must never overlap.

## 4. Superblock

The Superblock contains the main information required to identify and manage a KPFS disk.

Its fields must include:

Field| Description
Magic Word| KPFS identification signature
Version| File system version
Sector Size| 512 bytes
Total Sectors| Total number of sectors on the disk
Total Capacity| Total disk capacity in bytes
Superblock Size| Number of sectors occupied by the Superblock
File Table Start| Starting sector of the File Table
File Table Size| Number of sectors occupied by the File Table
Data Region Start| Starting sector of the File Data
Filesystem ID| Unique identifier for the file system

### 1. The Magic Word must contain the bytes representing "KPFS".
### 2. The Version field must identify the file system format version.
### 3. Total Sectors must represent the actual available sector count of the storage device.
### 4. Total Capacity must represent the detected disk capacity in bytes.
### 5. Sector addresses and capacity-related fields must use 64-bit unsigned integers where appropriate.
### 6. The Superblock must be expandable to support future metadata fields.
### 7. Reserved fields must be initialized to zero.
### 8. The Superblock must be validated before the file system accesses other disk regions.

## 5. File Table

### 1. Each file must have one File Table entry.
### 2. The File Table must support dynamic expansion.
### 3. Each entry must contain:
   - File name
   - Starting sector
   - File size in bytes
   - Allocated sector count
   - Entry status
### 4. Free entries must be reusable.
### 5. File names must be case-sensitive.
### 6. Duplicate file names are not allowed.
### 7. File names must contain between 1 and 15 characters.
### 8. File names must not contain spaces or forward slashes.
### 9. File Table entries must be validated before accessing their corresponding file data.

## 6. File Storage

### 1. Each file must occupy a continuous sequence of sectors.

### 2. The required sector count must be calculated using:
   
   S = ceil(FileSize / SectorSize)

### 3. The file system must locate the first available continuous space large enough for the file.

### 4. Files must be read using their actual file size, not their allocated sector capacity.

### 5. Deleted file sectors must become available for reuse.

### 6. Replacing an existing file must preserve the old file until the new data has been successfully written and committed to the File Table.

### 7. File allocations must never overlap.

### 8. A file must never occupy sectors outside the Data Region.

### 9. Zero-length files must be supported without allocating data sectors.

## 7. Storage Compatibility

KPFS must support two storage backends.

Physical Storage

- USB flash drives.
- Other supported block storage devices.
- Physical disks exposed through the KpOS storage driver.

Virtual Storage

- Raw disk image files.
- Virtual disks exposed through a compatible block device interface.

Both backends must provide the same basic sector read and write interface.

The KPFS driver must not directly depend on the physical storage hardware.

## 8. Disk Formatting

### 1. Formatting must create a valid Superblock.
### 2. Formatting must initialize the File Table.
### 3. Formatting must initialize the Data Region metadata.
### 4. Formatting must detect the actual storage capacity.
### 5. The detected sector count and total capacity must be stored in the Superblock.
### 6. Formatting must erase all previous KPFS metadata and file references.
### 7. Formatting must never occur automatically on an unrecognized disk.
### 8. Formatting must require an explicit user command or authorized system operation.
### 9. The format operation must validate that the selected disk has sufficient capacity for the required metadata.

## 9. File Commands

Command| Description
format| Formats the disk
ls| Lists stored files
write| Creates or replaces a file
cat| Reads a file
rm| Deletes a file

## 10. Error Handling and Integrity

### 1. Every disk operation must validate sector boundaries.
### 2. Invalid Superblocks must be rejected.
### 3. File Table entries must be validated before accessing their data.
### 4. The file system must prevent overlapping file allocations.
### 5. Failed write operations must not silently corrupt existing file metadata.
### 6. Disk capacity must never be exceeded.
### 7. Unsupported file system versions must be rejected safely.
### 8. The file system must detect insufficient free space before allocating new data.
### 9. Invalid or corrupted metadata must never be trusted without validation.
### 10. Disk read and write failures must be reported to the operating system.