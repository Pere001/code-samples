// 
// A function that checks a collision between a circle and a triangular wall. Coordinate values are integers.
//
// Has a little bit of unincluded context.
//


b32 CircleWallCollision(wall *w, v2s center, s32 radius){
	v2 t[3] = {V2(w->p[0] - center), V2(w->p[1] - center), V2(w->p[2] - center)};
	v2 n[3] = {w->normals[0], w->normals[1], w->normals[2]};
	f32 r = (f32)radius;
	v2 o = V2(0);

	// This loop checks if the center is inside the triangle, or inside the rectangles
	// created by protruding each edge outwards by 'radius' (edge x radius).
	// If it's inside this we're done.
	// Otherwise we have to check if the center is inside the circles formed by growing
	// each vertex by radius.
	b32 collision = true;
	for(s32 i = 0; i < 3; i++){
		f32 proj = Dot(n[i], o - t[i]); // Center projected onto normal
		if (proj > r) // Too far outwards from the edge
			return false;

		if (proj < 0) // The center is inward: it might be inside the triangle.
			continue;
		
		f32 edgeProj  = Dot(V2(-n[i].y, n[i].x), o - t[i]);
		f32 edgeProj2 = Dot(V2(-n[i].y, n[i].x), o - t[(i + 1) % 3]);
		if (edgeProj < 0 || edgeProj2 > 0){
			// Center is outside the triangle and outside these edge*radius rectangles.
			collision = false;
		}else{
			// Center is in the edge*radius rectangle
			return true;
		}
	}
	
	if (!collision){
		// Center wasn't inside the triangle or the edge*radius rectangles.
		// The only chance for collision is if a vertex is inside the circle.
		for(s32 j = 0; j < 3; j++){
			f32 distanceSqr = LengthSqr(t[j]);
			if (distanceSqr < r*r)
				return true;
		}
		return false;
	}
	return true;
}
