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

#include <assert.h>

#include "entity.h"

uint32_t entity_gen_id(dict_entity_t dict) {
	assert(dict);

	dict_entity_it_t it;
	dict_entity_it(it, dict);

	uint32_t id = 0;

	while(!dict_entity_end_p(it)) {
		uint32_t key = dict_entity_ref(it)->key;

		if(key > id)
			id = key;

		dict_entity_next(it);
	}

	return id + 1;
}
