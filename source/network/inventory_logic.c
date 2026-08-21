/*
	Copyright (c) 2023 ByteBit/xtreme8000

	This file is part of CavEX.

	CavEX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	CavEX is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with CavEX.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "inventory_logic.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

// Pure nearest-player pick for item collection — see inventory_logic.h.
int pickup_nearest_player(bool has0, double x0, double y0, double z0,
						  bool has1, double x1, double y1, double z1,
						  double ix, double iy, double iz, double reach) {
	double r2 = reach * reach;
	double d0 = r2 + 1.0, d1 = r2 + 1.0;

	if(has0) {
		double dx = ix - x0, dy = iy - y0, dz = iz - z0;
		d0 = dx * dx + dy * dy + dz * dz;
	}

	if(has1) {
		double dx = ix - x1, dy = iy - y1, dz = iz - z1;
		d1 = dx * dx + dy * dy + dz * dz;
	}

	if(d0 <= r2 && (d0 <= d1 || d1 > r2))
		return 0;
	if(d1 <= r2)
		return 1;
	return -1;
}

bool inventory_collect(struct inventory* inv, struct item_data* item,
					   uint8_t* slot_priority, size_t slot_length,
					   set_inv_slot_t changes) {
	assert(inv && item && item->id != 0 && changes);

	struct item* it = item_get(item);

	if(!it)
		return false;

	while(item->count > 0) {
		bool has_canidate_equal = false;
		size_t candidate_equal = 0;
		bool has_canidate_empty = false;
		size_t candidate_empty = 0;

		for(size_t k = 0; k < slot_length; k++) {
			uint8_t slot = slot_priority[k];

			if(inv->items[slot].id == item->id
			   && inv->items[slot].durability == item->durability
			   && inv->items[slot].count < it->max_stack) {
				has_canidate_equal = true;
				candidate_equal = slot;
				break;
			}

			if(!has_canidate_empty && inv->items[slot].id == 0) {
				has_canidate_empty = true;
				candidate_empty = slot;
			}
		}

		if(has_canidate_equal || has_canidate_empty) {
			size_t candidate
				= has_canidate_equal ? candidate_equal : candidate_empty;
			size_t additional
				= min(it->max_stack - inv->items[candidate].count, item->count);
			inv->items[candidate].id = item->id;
			inv->items[candidate].durability = item->durability;
			inv->items[candidate].count += additional;
			item->count -= additional;
			set_inv_slot_push(changes, candidate);
		} else {
			return false;
		}
	}

	return true;
}