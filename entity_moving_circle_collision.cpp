//
// A collision function from my game.
// 
// Has a lot of unincluded context.
//


// - Detects the collision between an entity (point) of a certain type, and a circle with radius
// 'r' "slided" on a line from 'p0' to 'p1' (i.e. making a pill shape).
// - Returns the first found entity that collided, or 0 if none.
entity *EntityMovingCircleCollision(world *w, entity_type entityType, s32 r, v2s p1, v2s p0, u64 eidException = 0){
	v2s lineCenter = p0 + (p1 - p0)/2;

	s32 margin = 100;
	v2s bboxMin = MinV2S(p0, p1) - V2S(r + margin);
	v2s bboxMax = MaxV2S(p0, p1) + V2S(r + margin);

	v2s chunkPosMin = WorldPosToChunk(w, ClampWorldPos(w, bboxMin));
	v2s chunkPosMax = WorldPosToChunk(w, ClampWorldPos(w, bboxMax));

	// Computing the intersection between a circle and a point is faster than between a circle
    // and line. So, we make a big circle that contains the "pill shape". If an entity collides
    // with this, then we'll check the more expensive line/circle collision.
	s64 earlyOutCircleRSqr = SquareS64((s64)r + (s64)Length(V2(p1 - p0))/2);
	s64 rSqr = SQUARE((s64)r);

	v2 lineNormal = Rotate90Degrees(V2(p1 - p0)/Length(V2(p1 - p0)));

	for(s32 cx = chunkPosMin.x; cx <= chunkPosMax.x; cx++){
		for(s32 cy = chunkPosMin.y; cy <= chunkPosMax.y; cy++){
			v2s chunkPos = {cx, cy};
			auto c = GetChunk(w, chunkPos);
			if (c && (c->flags & ChunkFlags_Active)){
				for(auto block = c->hotEntities; block; block = block->nextBlock){
					for(u16 i = 0; i < block->numEntitiesInBlock; i++){
						auto e = (entity *)(block + 1) + i;
						if (e->type != entityType || (e->flags & EntityFlags_RemoveAtEndOfFrame))
							continue;

						v2s delta = e->pos - lineCenter;
						s64 disSqr = SQUARE((s64)delta.x) + SQUARE((s64)delta.y);
						if (disSqr > earlyOutCircleRSqr)
							continue;

						// Check vertex circles
						v2s delta0 = e->pos - p0;
						v2s delta1 = e->pos - p1;
						s64 disSqr0 = SQUARE((s64)delta0.x) + SQUARE((s64)delta0.y);
						s64 disSqr1 = SQUARE((s64)delta1.x) + SQUARE((s64)delta1.y);

						if (!(disSqr0 < rSqr || disSqr1 < rSqr)){
							// Check segment rectangle
							f32 lineDistance = Dot(lineNormal, V2(delta0));
							if (Abs(lineDistance) > (f32)r)
								continue;

							f32 cross0 = Cross(lineNormal, V2(delta0));
							f32 cross1 = Cross(lineNormal, V2(delta1));
							if (cross0 > 0 || cross1 < 0)
								continue;
						}
						
						if (e->eid == eidException)
							continue;

						return e;
					}
				}
			}
		}
	}
	return 0;
}
