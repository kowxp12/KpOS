KPFS v2 — File System Specification

1. General Rules

1. KPFS is the native file system designed for KpOS.
2. KPFS v2 introduces hierarchical directory support.
3. KPFS must support both virtual disk images and physical storage devices.
4. KPFS must not impose a predefined maximum disk capacity.
5. The actual disk capacity must be detected and stored in the Superblock.
6. All storage operations must use 512-byte sectors.
7. KPFS must use 64-bit unsigned integers for sector addresses and capacity-related calculations.
8. Every file and directory must have a unique ID.
9. Files and directories must use the same ID system.
10. File and directory IDs must remain unchanged throughout their lifetime.
11. Deleting an entry must not cause other IDs to be reassigned or rebuilt.
12. The file system must be independent of the underlying storage hardware.

2. ID Management

1. Every file and directory must receive a unique ID when created.
2. IDs must be assigned sequentially.
3. The root directory must use ID 0.
4. File and directory IDs must belong to the same global ID space.
5. An assigned ID must remain permanent for the lifetime of its entry.
6. Deleting an entry must not change the IDs of other entries.
7. Existing IDs must never be modified during file or directory operations.
8. Deleted IDs must not be automatically reused.
9. ID allocation must prevent duplicate IDs.
10. The file system must maintain a reliable way to track previously assigned IDs.

3. Sector Rules

1. Each sector must contain exactly 512 bytes.
2. All disk read and write operations must operate on complete sectors.
3. Files and directories must occupy the required storage sectors.
4. Unused bytes in allocated sectors must not be treated as valid file or directory data.
5. The actual number of available sectors must be detected from the storage device.
6. All sector calculations must prevent integer overflow and invalid disk access.

4. Disk Layout

The disk layout must be dynamically determined during formatting.

Region| Purpose
Superblock| Stores file system metadata
Entry Table| Stores file and directory metadata
Directory Data| Stores directory child ID lists
File Data| Stores actual file contents

1. The Superblock may occupy multiple consecutive sectors.
2. The Superblock size must be determined during formatting.
3. The Entry Table must support dynamic expansion.
4. Directory Data must support dynamic allocation and expansion.
5. File Data must use the available storage space.
6. No fixed disk size or fixed number of data sectors must be assumed.
7. Every disk region must have a clearly defined starting sector and size.
8. Disk regions and allocated data must never overlap.

5. Superblock

The Superblock contains the main information required to identify and manage a KPFS disk.

Its fields must include:

Field| Description
Magic Word| KPFS identification signature
Version| File system version
Sector Size| 512 bytes
Total Sectors| Total number of sectors on the disk
Total Capacity| Total disk capacity in bytes
Superblock Size| Number of sectors occupied by the Superblock
Entry Table Start| Starting sector of the Entry Table
Entry Table Size| Number of sectors occupied by the Entry Table
Directory Data Start| Starting sector of Directory Data
File Data Start| Starting sector of File Data
Filesystem ID| Unique identifier for the file system
Next Entry ID| Next available ID for a new entry

1. The Magic Word must contain the bytes representing "KPFS".
2. The Version field must identify the file system format version.
3. Total Sectors must represent the actual available sector count of the storage device.
4. Total Capacity must represent the detected disk capacity in bytes.
5. Sector addresses and capacity-related fields must use 64-bit unsigned integers where appropriate.
6. The Superblock must support future expansion.
7. Reserved fields must be initialized to zero.
8. The Superblock must be validated before accessing other disk regions.

6. Entry Metadata

Every file and directory must have an independent metadata entry.

All entries must contain a unique ID and the information required to identify their type.

File Metadata

Each file must store the following information:

Field| Description
File Name| Name of the file
File Format| File extension or format
File ID| Unique file identifier
Storage Location| Starting sector or storage location
File Size| Actual file size in bytes
Allocated Sectors| Number of occupied sectors

Directory Metadata

Each directory must store the following information:

Field| Description
Folder Name| Name of the directory
Directory ID| Unique directory identifier
Child ID List| IDs of files and directories inside it
Storage Location| Sector or storage location where its directory data is stored

1. Files and directories must use the same global ID system.
2. Every directory must have its own unique ID.
3. Every directory must maintain a list of its direct children.
4. Child ID lists must contain valid IDs referring to existing entries.
5. Directory metadata must not require files to store their parent directory IDs.
6. Each directory must have an independent storage location for its directory data.
7. The root directory must be the starting point of the directory hierarchy.

7. File Storage

1. Each file must occupy a continuous sequence of sectors.

2. The required sector count must be calculated using:
   
   S = ceil(FileSize / SectorSize)

3. The file system must locate available continuous space large enough for the file.

4. Files must be read using their actual file size, not their allocated sector capacity.

5. Deleted file sectors must become available for reuse.

6. Replacing an existing file must preserve the old file until the new data has been successfully written and committed.

7. File allocations must never overlap.

8. A file must never occupy sectors outside the File Data region.

9. Zero-length files must be supported without allocating data sectors.

10. File IDs must remain unchanged when file contents are modified or replaced.

8. Directory Management

1. Directories must support hierarchical organization.
2. Each directory must have a unique ID.
3. Each directory must maintain a Child ID List.
4. The Child ID List must contain the IDs of all direct files and directories inside that directory.
5. Directories must not store complete copies of their children's metadata.
6. Directory contents must be resolved through the global Entry Table.
7. Directories must support multiple levels of nesting.
8. Directory names must be unique within the same parent directory.
9. Directory storage must be dynamically allocated.
10. A directory must not reference itself as a child.
11. Circular directory references must be prevented.
12. The root directory must not be deleted.

9. File and Directory Operations

File Creation

1. A new file must receive a unique ID.
2. Its metadata must be registered in the Entry Table.
3. Its ID must be added to the destination directory's Child ID List.
4. Its data must be allocated in the File Data region.

File Movement

1. Moving a file must not modify its File ID.
2. Moving a file must not modify its actual file data.
3. Moving a file must not change its storage location.
4. The file ID must be removed from the source directory's Child ID List.
5. The same ID must be added to the destination directory's Child ID List.
6. The operation must preserve directory consistency.

Directory Movement

1. Moving a directory must not modify its Directory ID.
2. Its child IDs must remain unchanged.
3. Its directory data must remain unchanged.
4. Only the directory references in the relevant parent directories must be updated.
5. Moving a directory into itself or one of its descendants must be prohibited.

Renaming

1. Renaming a file must not change its ID.
2. Renaming a directory must not change its ID.
3. Renaming must not modify the storage location or file contents.
4. The new name must be validated before committing the change.

10. File Formats

1. The operating system must determine the file format using the file extension.
2. The extension is the text following the last dot in the file name.
3. File extensions must be handled independently from file IDs.
4. File format information must not determine the physical storage location.
5. Files without extensions must also be supported.

11. Disk Formatting

1. Formatting must create a valid Superblock.
2. Formatting must initialize the Entry Table.
3. Formatting must initialize the Root Directory.
4. Formatting must initialize Directory Data management.
5. Formatting must initialize File Data management.
6. Formatting must detect the actual storage capacity.
7. The detected sector count and total capacity must be stored in the Superblock.
8. Formatting must erase all previous KPFS metadata and file references.
9. Formatting must never occur automatically on an unrecognized disk.
10. Formatting must require an explicit user command or authorized system operation.
11. Formatting must validate that the selected disk has sufficient capacity for the required metadata.

12. File Commands

Command| Description
format| Formats the disk
ls| Lists files and directories
write| Creates or replaces a file
cat| Reads a file
rm| Deletes a file or directory
mkdir| Creates a directory
cd| Changes the current directory
pwd| Displays the current directory path
mv| Moves a file or directory
rename| Renames a file or directory

13. Deletion Rules

1. Deleting a file must remove its ID from its parent directory's Child ID List.
2. Deleting a file must release its allocated data sectors.
3. Deleting a directory must require the user to confirm what happens to its contents.
4. The rm command must support an option to perform deletion without asking for confirmation.
5. Without that option, the operating system must ask the user for confirmation when deleting a non-empty directory.
6. Directory deletion must correctly handle all child entries.
7. Deleted IDs must not cause other entries to receive new IDs.
8. Deletion must preserve the consistency of the Entry Table and Directory Data.

14. Error Handling and Integrity

1. Every disk operation must validate sector boundaries.
2. Invalid Superblocks must be rejected.
3. Entry Table records must be validated before accessing their data.
4. Directory Child ID Lists must be validated before traversal.
5. The file system must prevent overlapping storage allocations.
6. Disk capacity must never be exceeded.
7. Unsupported file system versions must be rejected safely.
8. Failed write operations must not silently corrupt existing metadata.
9. Insufficient free space must be detected before allocation.
10. Invalid or corrupted metadata must never be trusted without validation.
11. Disk read and write failures must be reported to the operating system.
12. Directory operations must preserve the consistency of all affected entries.

15. Storage Compatibility

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