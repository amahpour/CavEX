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

#include "../item/inventory.h"

bool inventory_collect(struct inventory* inv, struct item_data* item,
					   uint8_t* slot_priority, size_t slot_length,
					   set_inv_slot_t changes);

// Which of two local players collects an item lying at (ix,iy,iz), given each
// player's presence flag, position and pickup radius`reach` (issue #139):
// returns 0 or 1 for the NEAREST in-range player, -1 when neither is in
// range. Pure (no engine state) so it is unit-testable.
int pickup_nearest_player(bool has0, double x0, double y0, double z0,
						  bool has1, double x1, double y1, double z1,
						  double ix, double iy, double iz, double reach);

extern struct inventory_logic inventory_logic_player;
extern struct inventory_logic inventory_logic_crafting;
extern struct inventory_logic inventory_logic_furnace;