#ifndef RC_STORAGE_H
#define RC_STORAGE_H

#include "types.h"

enum {
    RC_STORAGE_NONE = 0,
    RC_STORAGE_BANK = 1,
    RC_STORAGE_DEPOSIT_BOX = 2,
    RC_STORAGE_COLLECTION = 3,
    RC_STORAGE_CONTAINER = 4,
};

int rc_storage_kind_for_object(int obj_id, int option);
int rc_player_open_storage_object(RcWorld *world, int obj_id, int option);
int rc_bank_deposit_slot(RcWorld *world, int inv_slot, int quantity);
int rc_bank_withdraw_slot(RcWorld *world, int bank_slot, int quantity);

#endif
