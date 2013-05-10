/*
** fatxxfs
** The Sleuth Kit 
**
** Content and meta data layer support for the FATXX file system 
**
** Brian Carrier [carrier <at> sleuthkit [dot] org]
** Copyright (c) 2006-2013 Brian Carrier, Basis Technology. All Rights reserved
** Copyright (c) 2003-2005 Brian Carrier.  All rights reserved 
**
** TASK
** Copyright (c) 2002 Brian Carrier, @stake Inc.  All rights reserved
**
**
** This software is distributed under the Common Public License 1.0
**
** Unicode added with support from I.D.E.A.L. Technology Corp (Aug '05)
**
*/

// RJCTODO: Update comments
/**
 * \file fatxxfs.c
 * Contains the internal TSK FATXX (FAT12, FAT16, FAT32) file system code to 
 * handle basic file system processing for opening file system, processing 
 * sectors, and directory entries. 
 */

#include "tsk_fatxxfs.h"

uint8_t
fatxxfs_is_dentry(FATFS_INFO * fatfs, FATFS_DENTRY * a_de, uint8_t a_basic)
{
    const char *func_name = "fatxxfs_is_dentry";
    TSK_FS_INFO *fs = (TSK_FS_INFO *) & fatfs->fs_info;
    FATXXFS_DENTRY *de = (FATXXFS_DENTRY *) a_de; 

    if (!de)
        return 0;

    /* LFN have their own checks, which are pretty weak since most
     * fields are UTF16 */
    if ((de->attrib & FATFS_ATTR_LFN) == FATFS_ATTR_LFN) {
        fatfs_dentry_lfn *de_lfn = (fatfs_dentry_lfn *) de;

        if ((de_lfn->seq > (FATFS_LFN_SEQ_FIRST | 0x0f))
            && (de_lfn->seq != FATFS_SLOT_DELETED)) {
            if (tsk_verbose)
                fprintf(stderr, "%s: LFN seq\n", func_name);
            return 0;
        }

        return 1;
    }
    else {
        // the basic test is only for the 'essential data'.
        if (a_basic == 0) {
            if (de->lowercase & ~(FATFS_CASE_LOWER_ALL)) {
                if (tsk_verbose)
                    fprintf(stderr, "%s: lower case all\n", func_name);
                return 0;
            }
            else if (de->attrib & ~(FATFS_ATTR_ALL)) {
                if (tsk_verbose)
                    fprintf(stderr, "%s: attribute all\n", func_name);
                return 0;
            }

            // verify we do not have too many flags set
            if (de->attrib & FATFS_ATTR_VOLUME) {
                if ((de->attrib & FATFS_ATTR_DIRECTORY) ||
                    (de->attrib & FATFS_ATTR_READONLY) ||
                    (de->attrib & FATFS_ATTR_ARCHIVE)) {
                    if (tsk_verbose)
                        fprintf(stderr,
                            "%s: Vol and Dir/RO/Arch\n", func_name);
                    return 0;
                }
            }

            /* The ctime, cdate, and adate fields are optional and 
             * therefore 0 is a valid value
             * We have had scenarios where ISDATE and ISTIME return true,
             * but the unix2dos fail during the conversion.  This has been
             * useful to detect corrupt entries, so we do both. 
             */
            if ((tsk_getu16(fs->endian, de->ctime) != 0) &&
                (FATFS_ISTIME(tsk_getu16(fs->endian, de->ctime)) == 0)) {
                if (tsk_verbose)
                    fprintf(stderr, "%s: ctime\n", func_name);
                return 0;
            }
            else if ((tsk_getu16(fs->endian, de->wtime) != 0) &&
                (FATFS_ISTIME(tsk_getu16(fs->endian, de->wtime)) == 0)) {
                if (tsk_verbose)
                    fprintf(stderr, "%s: wtime\n", func_name);
                return 0;
            }
            else if ((tsk_getu16(fs->endian, de->cdate) != 0) &&
                ((FATFS_ISDATE(tsk_getu16(fs->endian, de->cdate)) == 0) ||
                    (dos2unixtime(tsk_getu16(fs->endian, de->cdate),
                            tsk_getu16(fs->endian, de->ctime),
                            de->ctimeten) == 0))) {
                if (tsk_verbose)
                    fprintf(stderr, "%s: cdate\n", func_name);
                return 0;
            }
            else if (de->ctimeten > 200) {
                if (tsk_verbose)
                    fprintf(stderr, "%s: ctimeten\n", func_name);
                return 0;
            }
            else if ((tsk_getu16(fs->endian, de->adate) != 0) &&
                ((FATFS_ISDATE(tsk_getu16(fs->endian, de->adate)) == 0) ||
                    (dos2unixtime(tsk_getu16(fs->endian, de->adate),
                            0, 0) == 0))) {
                if (tsk_verbose)
                    fprintf(stderr, "%s: adate\n", func_name);
                return 0;
            }
            else if ((tsk_getu16(fs->endian, de->wdate) != 0) &&
                ((FATFS_ISDATE(tsk_getu16(fs->endian, de->wdate)) == 0) ||
                    (dos2unixtime(tsk_getu16(fs->endian, de->wdate),
                            tsk_getu16(fs->endian, de->wtime), 0) == 0))) {
                if (tsk_verbose)
                    fprintf(stderr, "%s: wdate\n", func_name);
                return 0;
            }
        }

        /* verify the starting cluster is small enough */
        if ((FATFS_DENTRY_CLUST(fs, de) > (fatfs->lastclust)) &&
            (FATFS_ISEOF(FATFS_DENTRY_CLUST(fs, de), fatfs->mask) == 0)) {
            if (tsk_verbose)
                fprintf(stderr, "%s: start cluster\n", func_name);
            return 0;
        }

        /* Verify the file size is smaller than the data area */
        else if (tsk_getu32(fs->endian, de->size) >
            ((fatfs->clustcnt * fatfs->csize) << fatfs->ssize_sh)) {
            if (tsk_verbose)
                fprintf(stderr, "%s: size\n", func_name);
            return 0;
        }

        else if ((tsk_getu32(fs->endian, de->size) > 0)
            && (FATFS_DENTRY_CLUST(fs, de) == 0)) {
            if (tsk_verbose)
                fprintf(stderr,
                    "%s: non-zero size and NULL starting cluster\n", func_name);
            return 0;
        }

        else if (is_83_name(de) == 0)
            return 0;

        // basic sanity check on values
        else if ((tsk_getu16(fs->endian, de->ctime) == 0)
            && (tsk_getu16(fs->endian, de->wtime) == 0)
            && (tsk_getu16(fs->endian, de->cdate) == 0)
            && (tsk_getu16(fs->endian, de->adate) == 0)
            && (tsk_getu16(fs->endian, de->wdate) == 0)
            && (FATFS_DENTRY_CLUST(fs, de) == 0)
            && (tsk_getu32(fs->endian, de->size) == 0)) {
            if (tsk_verbose)
                fprintf(stderr,
                    "s: nearly all values zero\n", func_name);
            return 0;
        }

        return 1;
    }
}

/* timetens is number of tenths of a second for a 2 second range (values 0 to 199) */
static uint32_t
dos2nanosec(uint8_t timetens)
{
    timetens %= 100;
    return timetens * 10000000;
}

/*
 * convert the attribute list in FAT to a UNIX mode
 */
static TSK_FS_META_TYPE_ENUM
attr2type(uint16_t attr)
{
    if (attr & FATFS_ATTR_DIRECTORY)
        return TSK_FS_META_TYPE_DIR;
    else
        return TSK_FS_META_TYPE_REG;
}

static int
attr2mode(uint16_t attr)
{
    int mode;

    /* every file is executable */
    mode =
        (TSK_FS_META_MODE_IXUSR | TSK_FS_META_MODE_IXGRP |
        TSK_FS_META_MODE_IXOTH);

    if ((attr & FATFS_ATTR_READONLY) == 0)
        mode |=
            (TSK_FS_META_MODE_IRUSR | TSK_FS_META_MODE_IRGRP |
            TSK_FS_META_MODE_IROTH);

    if ((attr & FATFS_ATTR_HIDDEN) == 0)
        mode |=
            (TSK_FS_META_MODE_IWUSR | TSK_FS_META_MODE_IWGRP |
            TSK_FS_META_MODE_IWOTH);

    return mode;
}


TSK_RETVAL_ENUM
fatxxfs_dinode_copy(FATFS_INFO * fatfs, TSK_FS_META * fs_meta,
    FATFS_DENTRY * a_in, TSK_DADDR_T sect, TSK_INUM_T inum)
{
    const char *func_name = "fatxxfs_dinode_copy";
    int retval;
    int i;
    TSK_FS_INFO *fs = (TSK_FS_INFO *) & fatfs->fs_info;
    TSK_DADDR_T *addr_ptr;
    FATXXFS_DENTRY *in = (FATXXFS_DENTRY *) a_in;

    if (fs_meta->content_len < FATFS_FILE_CONTENT_LEN) {
        if ((fs_meta =
                tsk_fs_meta_realloc(fs_meta,
                    FATFS_FILE_CONTENT_LEN)) == NULL) {
            return TSK_ERR;
        }
    }

    fs_meta->attr_state = TSK_FS_META_ATTR_EMPTY;
    if (fs_meta->attr) {
        tsk_fs_attrlist_markunused(fs_meta->attr);
    }

    fs_meta->mode = attr2mode(in->attrib);
    fs_meta->type = attr2type(in->attrib);

    fs_meta->addr = inum;

    /* Use the allocation status of the sector to determine if the
     * dentry is allocated or not */
    retval = fatfs_is_sectalloc(fatfs, sect);
    if (retval == -1) {
        return TSK_ERR;
    }
    else if (retval == 1) {
        fs_meta->flags = ((in->name[0] == FATFS_SLOT_DELETED) ?
            TSK_FS_META_FLAG_UNALLOC : TSK_FS_META_FLAG_ALLOC);
    }
    else {
        fs_meta->flags = TSK_FS_META_FLAG_UNALLOC;
    }

    /* Slot has not been used yet */
    fs_meta->flags |= ((in->name[0] == FATFS_SLOT_EMPTY) ?
        TSK_FS_META_FLAG_UNUSED : TSK_FS_META_FLAG_USED);

    if ((in->attrib & FATFS_ATTR_LFN) == FATFS_ATTR_LFN) {
        /* LFN entries don't have these values */
        fs_meta->nlink = 0;
        fs_meta->size = 0;
        fs_meta->mtime = 0;
        fs_meta->atime = 0;
        fs_meta->ctime = 0;
        fs_meta->crtime = 0;
        fs_meta->mtime_nano = fs_meta->atime_nano = fs_meta->ctime_nano =
            fs_meta->crtime_nano = 0;
    }
    else {
        /* There is no notion of link in FAT, just deleted or not */
        fs_meta->nlink = (in->name[0] == FATFS_SLOT_DELETED) ? 0 : 1;
        fs_meta->size = (TSK_OFF_T) tsk_getu32(fs->endian, in->size);

        /* If these are valid dates, then convert to a unix date format */
        if (FATFS_ISDATE(tsk_getu16(fs->endian, in->wdate)))
            fs_meta->mtime =
                dos2unixtime(tsk_getu16(fs->endian, in->wdate),
                tsk_getu16(fs->endian, in->wtime), 0);
        else
            fs_meta->mtime = 0;
        fs_meta->mtime_nano = 0;

        if (FATFS_ISDATE(tsk_getu16(fs->endian, in->adate)))
            fs_meta->atime =
                dos2unixtime(tsk_getu16(fs->endian, in->adate), 0, 0);
        else
            fs_meta->atime = 0;
        fs_meta->atime_nano = 0;


        /* cdate is the creation date in FAT and there is no change,
         * so we just put in into change and set create to 0.  The other
         * front-end code knows how to handle it and display it
         */
        if (FATFS_ISDATE(tsk_getu16(fs->endian, in->cdate))) {
            fs_meta->crtime =
                dos2unixtime(tsk_getu16(fs->endian, in->cdate),
                tsk_getu16(fs->endian, in->ctime), in->ctimeten);
            fs_meta->crtime_nano = dos2nanosec(in->ctimeten);
        }
        else {
            fs_meta->crtime = 0;
            fs_meta->crtime_nano = 0;
        }

        // FAT does not have a changed time
        fs_meta->ctime = 0;
        fs_meta->ctime_nano = 0;
    }

    /* Values that do not exist in FAT */
    fs_meta->uid = 0;
    fs_meta->gid = 0;
    fs_meta->seq = 0;


    /* We will be copying a name, so allocate a structure */
    if (fs_meta->name2 == NULL) {
        if ((fs_meta->name2 = (TSK_FS_META_NAME_LIST *)
                tsk_malloc(sizeof(TSK_FS_META_NAME_LIST))) == NULL)
            return TSK_ERR;
        fs_meta->name2->next = NULL;
    }

    /* If we have a LFN entry, then we need to convert the three
     * parts of the name to UTF-8 and copy it into the name structure .
     */
    if ((in->attrib & FATFS_ATTR_LFN) == FATFS_ATTR_LFN) {
        fatfs_dentry_lfn *lfn = (fatfs_dentry_lfn *) in;

        /* Convert the first part of the name */
        UTF8 *name8 = (UTF8 *) fs_meta->name2->name;
        UTF16 *name16 = (UTF16 *) lfn->part1;

        int retVal = tsk_UTF16toUTF8(fs->endian, (const UTF16 **) &name16,
            (UTF16 *) & lfn->part1[10],
            &name8,
            (UTF8 *) ((uintptr_t) fs_meta->name2->name +
                sizeof(fs_meta->name2->name)),
            TSKlenientConversion);

        if (retVal != TSKconversionOK) {
            tsk_error_reset();
            tsk_error_set_errno(TSK_ERR_FS_UNICODE);
            tsk_error_set_errstr
                ("%s: Error converting FAT LFN (1) to UTF8: %d",
                func_name, retVal);
            *name8 = '\0';

            return TSK_COR;
        }

        /* Convert the second part of the name */
        name16 = (UTF16 *) lfn->part2;
        retVal = tsk_UTF16toUTF8(fs->endian, (const UTF16 **) &name16,
            (UTF16 *) & lfn->part2[12],
            &name8,
            (UTF8 *) ((uintptr_t) fs_meta->name2->name +
                sizeof(fs_meta->name2->name)), TSKlenientConversion);

        if (retVal != TSKconversionOK) {
            tsk_error_reset();
            tsk_error_set_errno(TSK_ERR_FS_UNICODE);
            tsk_error_set_errstr
                ("%s: Error converting FAT LFN (2) to UTF8: %d",
                func_name, retVal);
            *name8 = '\0';

            return TSK_COR;
        }

        /* Convert the third part of the name */
        name16 = (UTF16 *) lfn->part3;
        retVal = tsk_UTF16toUTF8(fs->endian, (const UTF16 **) &name16,
            (UTF16 *) & lfn->part3[4],
            &name8,
            (UTF8 *) ((uintptr_t) fs_meta->name2->name +
                sizeof(fs_meta->name2->name)), TSKlenientConversion);

        if (retVal != TSKconversionOK) {
            tsk_error_reset();
            tsk_error_set_errno(TSK_ERR_FS_UNICODE);
            tsk_error_set_errstr
                ("%s: Error converting FAT LFN (3) to UTF8: %d",
                func_name, retVal);
            *name8 = '\0';

            return TSK_COR;
        }

        /* Make sure it is NULL Terminated */
        if ((uintptr_t) name8 >
            (uintptr_t) fs_meta->name2->name +
            sizeof(fs_meta->name2->name))
            fs_meta->name2->name[sizeof(fs_meta->name2->name) - 1] = '\0';
        else
            *name8 = '\0';
    }
    /* If the entry is for a volume label, then copy the label.
     */
    else if ((in->attrib & FATFS_ATTR_VOLUME) == FATFS_ATTR_VOLUME) {
        int a;

        i = 0;
        for (a = 0; a < 8; a++) {
            if ((in->name[a] != 0x00) && (in->name[a] != 0xff))
                fs_meta->name2->name[i++] = in->name[a];
        }
        for (a = 0; a < 3; a++) {
            if ((in->ext[a] != 0x00) && (in->ext[a] != 0xff))
                fs_meta->name2->name[i++] = in->ext[a];
        }
        fs_meta->name2->name[i] = '\0';

        /* clean up non-ASCII because we are
         * copying it into a buffer that is supposed to be UTF-8 and
         * we don't know what encoding it is actually in or if it is 
         * simply junk. */
        fatfs_cleanup_ascii(fs_meta->name2->name);
    }
    /* If the entry is a normal short entry, then copy the name
     * and add the '.' for the extension
     */
    else {
        for (i = 0; (i < 8) && (in->name[i] != 0) && (in->name[i] != ' ');
            i++) {
            if ((i == 0) && (in->name[0] == FATFS_SLOT_DELETED))
                fs_meta->name2->name[0] = '_';
            else if ((in->lowercase & FATFS_CASE_LOWER_BASE) &&
                (in->name[i] >= 'A') && (in->name[i] <= 'Z'))
                fs_meta->name2->name[i] = in->name[i] + 32;
            else
                fs_meta->name2->name[i] = in->name[i];
        }

        if ((in->ext[0]) && (in->ext[0] != ' ')) {
            int a;
            fs_meta->name2->name[i++] = '.';
            for (a = 0;
                (a < 3) && (in->ext[a] != 0) && (in->ext[a] != ' ');
                a++, i++) {
                if ((in->lowercase & FATFS_CASE_LOWER_EXT)
                    && (in->ext[a] >= 'A') && (in->ext[a] <= 'Z'))
                    fs_meta->name2->name[i] = in->ext[a] + 32;
                else
                    fs_meta->name2->name[i] = in->ext[a];
            }
        }
        fs_meta->name2->name[i] = '\0';

        /* clean up non-ASCII because we are
         * copying it into a buffer that is supposed to be UTF-8 and
         * we don't know what encoding it is actually in or if it is 
         * simply junk. */
        fatfs_cleanup_ascii(fs_meta->name2->name);
    }

    /* Clean up name to remove control characters */
    i = 0;
    while (fs_meta->name2->name[i] != '\0') {
        if (TSK_IS_CNTRL(fs_meta->name2->name[i]))
            fs_meta->name2->name[i] = '^';
        i++;
    }

    /* get the starting cluster */
    addr_ptr = (TSK_DADDR_T *) fs_meta->content_ptr;
    if ((in->attrib & FATFS_ATTR_LFN) == FATFS_ATTR_LFN) {
        addr_ptr[0] = 0;
    }
    else {
        addr_ptr[0] = FATFS_DENTRY_CLUST(fs, in) & fatfs->mask;
    }

    /* FAT does not store a size for its directories so make one based
     * on the number of allocated sectors
     */
    if ((in->attrib & FATFS_ATTR_DIRECTORY) &&
        ((in->attrib & FATFS_ATTR_LFN) != FATFS_ATTR_LFN)) {
        if (fs_meta->flags & TSK_FS_META_FLAG_ALLOC) {
            TSK_LIST *list_seen = NULL;

            /* count the total number of clusters in this file */
            TSK_DADDR_T clust = FATFS_DENTRY_CLUST(fs, in);
            int cnum = 0;

            while ((clust) && (0 == FATFS_ISEOF(clust, fatfs->mask))) {
                TSK_DADDR_T nxt;

                /* Make sure we do not get into an infinite loop */
                if (tsk_list_find(list_seen, clust)) {
                    if (tsk_verbose)
                        tsk_fprintf(stderr,
                            "Loop found while determining directory size\n");
                    break;
                }
                if (tsk_list_add(&list_seen, clust)) {
                    tsk_list_free(list_seen);
                    list_seen = NULL;
                    return TSK_ERR;
                }

                cnum++;

                if (fatfs_getFAT(fatfs, clust, &nxt))
                    break;
                else
                    clust = nxt;
            }

            tsk_list_free(list_seen);
            list_seen = NULL;

            fs_meta->size =
                (TSK_OFF_T) ((cnum * fatfs->csize) << fatfs->ssize_sh);
        }
        /* if the dir is unallocated, then assume 0 or cluster size
         * Ideally, we would have a smart algo here to do recovery
         * and look for dentries.  However, we do not have that right
         * now and if we do not add this special check then it can
         * assume that an allocated file cluster chain belongs to the
         * directory */
        else {
            // if the first cluster is allocated, then set size to be 0
            if (fatfs_is_clustalloc(fatfs, FATFS_DENTRY_CLUST(fs,
                        in)) == 1)
                fs_meta->size = 0;
            else
                fs_meta->size = fatfs->csize << fatfs->ssize_sh;
        }
    }

    return TSK_OK;
}
