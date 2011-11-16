// Copyright (c) Cronus Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _INT_COOLDOWN_H_
#define _INT_COOLDOWN_H_

struct skill_cooldown_data;

struct cddata {
	int account_id, char_id;
	int count;
	struct skill_cooldown_data* data;
};

extern char skill_cooldown_txt[1024];

struct cddata* cooldown_search_cddata(int aid, int cid);
void cooldown_delete_cddata(int aid, int cid);
void inter_cooldown_save(void);
void cooldown_init(void);
void cooldown_final(void);

#endif /* _INT_COOLDOWN_H_ */
