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

#include <m-lib/m-array.h>

#include "entity.h"

// Scratch list of entity keys collected during a tick walk, erased afterwards.
ARRAY_DEF(entity_key_list, uint32_t)

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

size_t entity_tick_all(dict_entity_t dict, entity_tick_fn cb, void* ctx) {
	assert(dict);
	assert(cb);

	// Pass 1: walk the dict, run the callback, and collect keys to remove.
	// Nothing is erased here, so the iterator stays valid for the whole walk.
	entity_key_list_t remove;
	entity_key_list_init(remove);

	dict_entity_it_t it;
	dict_entity_it(it, dict);

	while(!dict_entity_end_p(it)) {
		uint32_t key = dict_entity_ref(it)->key;
		struct entity* e = &dict_entity_ref(it)->value;

		if(cb(key, e, ctx))
			entity_key_list_push_back(remove, key);

		dict_entity_next(it);
	}

	// Pass 2: the iterator is released, so erasing (which may resize and
	// relocate the open-addressing buckets) is now safe.
	size_t count = entity_key_list_size(remove);
	for(size_t i = 0; i < count; i++)
		dict_entity_erase(dict, *entity_key_list_get(remove, i));

	entity_key_list_clear(remove);
	return count;
}
