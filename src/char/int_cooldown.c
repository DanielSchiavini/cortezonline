// Copyright (c) Cronus Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/mmo.h"
#include "../common/db.h"
#include "../common/lock.h"
#include "../common/malloc.h"
#include "../common/showmsg.h"
#include "int_cooldown.h"

#include <stdio.h>

static DBMap* skill_cooldown_db = NULL;
char skill_cooldown_txt[1024]="save/cooldown_data.txt";

static void* create_cddata(DBKey key, va_list args)
{
	struct cddata *data;
	data = (struct cddata*)aCalloc(1, sizeof(struct cddata));
	data->account_id = va_arg(args, int);
	data->char_id = key.i;
	return data;
}

/*==========================================
 * Loads cooldown data of the player given. [Skotlex]
 *------------------------------------------*/
struct cddata* cooldown_search_cddata(int aid, int cid)
{
	return (struct cddata*)skill_cooldown_db->ensure(skill_cooldown_db, i2key(cid), create_cddata, aid);
}

/*==========================================
 * Deletes cooldown data of the player given.
 * Should be invoked after the data of said player was successfully loaded.
 *------------------------------------------*/
void cooldown_delete_cddata(int aid, int cid)
{
	struct cddata* cddata = (struct cddata*)idb_remove(skill_cooldown_db, cid);
	if (cddata)
	{
		if (cddata->data)
			aFree(cddata->data);
		aFree(cddata);
	}
}


static void inter_cooldown_tostr(char* line, struct cddata *cd_data)
{
	int i, len;

	len = sprintf(line, "%d,%d,%d\t", cd_data->account_id, cd_data->char_id, cd_data->count);
	for(i = 0; i < cd_data->count; i++)
		len += sprintf(line + len, "%hu,%ld\t", cd_data->data[i].skill_id, cd_data->data[i].tick);
	return;
}

static int inter_cooldown_fromstr(char *line, struct cddata *cd_data)
{
	int i, len, next;
	
	if (sscanf(line,"%d,%d,%d\t%n",&cd_data->account_id, &cd_data->char_id, &cd_data->count, &next) < 3)
		return 0;

	if (cd_data->count < 1)
		return 0;
	
	cd_data->data = (struct skill_cooldown_data*)aCalloc(cd_data->count, sizeof (struct skill_cooldown_data));

	for (i = 0; i < cd_data->count; i++)
	{
		if (sscanf(line + next, "%hu,%ld\t%n", &cd_data->data[i].skill_id, &cd_data->data[i].tick, &len) < 3)
		{
			aFree(cd_data->data);
			return 0;
		}
		next+=len;
	}
	return 1;
}
/*==========================================
 * Loads all cddata from the given filename.
 *------------------------------------------*/
void cooldown_load_cddata(const char* filename)
{
	FILE *fp;
	int sd_count=0, cd_count=0;
	char line[8192];
	struct cddata *cd;

	if ((fp = fopen(filename, "r")) == NULL) {
		ShowError("cooldown_load_cddata: Cannot open file %s!\n", filename);
		return;
	}

	while(fgets(line, sizeof(line), fp))
	{
		cd = (struct cddata*)aCalloc(1, sizeof(struct cddata));
		if (inter_cooldown_fromstr(line, cd)) {
			sd_count++;
			cd_count+= cd->count;
			cd = (struct cddata*)idb_put(skill_cooldown_db, cd->char_id, cd);
			if (cd) {
				ShowError("Duplicate entry in %s for character %d\n", filename, cd->char_id);
				if (cd->data) aFree(cd->data);
				aFree(cd);
			}
		} else {
			ShowError("cooldown_load_cddata: Broken line data: %s\n", line);
			aFree(cd);
		}
	}
	fclose(fp);
	ShowStatus("Loaded %d saved cooldowns for %d characters.\n", cd_count, sd_count);
}

static int inter_cooldown_save_sub(DBKey key, void *data, va_list ap) {
	char line[8192];
	struct cddata * cd_data;
	FILE *fp;

	cd_data = (struct cddata *)data;
	if (cd_data->count < 1)
		return 0;
	
	fp = va_arg(ap, FILE *);
	inter_cooldown_tostr(line, cd_data);
	fprintf(fp, "%s\n", line);
	return 0;
}

/*==========================================
 * Saves all cddata to the given filename.
 *------------------------------------------*/
void inter_cooldown_save()
{
	FILE *fp;
	int lock;

	if ((fp = lock_fopen(skill_cooldown_txt, &lock)) == NULL) {
		ShowError("int_cooldown: can't write [%s] !!! data is lost !!!\n", skill_cooldown_txt);
		return;
	}
	skill_cooldown_db->foreach(skill_cooldown_db, inter_cooldown_save_sub, fp);
	lock_fclose(fp,skill_cooldown_txt, &lock);
}

/*==========================================
 * Initializes db.
 *------------------------------------------*/
void cooldown_init()
{
	skill_cooldown_db = idb_alloc(DB_OPT_BASE);
	cooldown_load_cddata(skill_cooldown_txt);
}

/*==========================================
 * Frees up memory.
 *------------------------------------------*/
static int skill_cooldown_db_final(DBKey k,void *d,va_list ap)
{
	struct cddata *data = (struct cddata*)d;
	if (data->data)
		aFree(data->data);
	aFree(data);
	return 0;
}

/*==========================================
 * Final cleanup.
 *------------------------------------------*/
void cooldown_final(void)
{
	skill_cooldown_db->destroy(skill_cooldown_db, skill_cooldown_db_final);
}
