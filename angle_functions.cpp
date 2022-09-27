//
// Small angle functions that I grabbed from my game.
//
typedef float f32;
#define PI 3.141592653589793238462f
#include <math.h>


// Range [0, 2*PI)
f32 NormalizeAngle(f32 a){
	f32 result = fmod(a, 2*PI);
	if (result < 0)
		result += 2*PI;
	return result;
}

// Range (-PI, PI] 
f32 NormalizeAngleMinusPiPi(f32 a){
	f32 result = NormalizeAngle(a);
	if (result > PI)
		result -= 2*PI;
	return result;
}

// - Returns the angle that you would have to add to the direction 'from' to  get to direction 'to'.
// - Range (-PI, PI] 
f32 AngleDifference(f32 to, f32 from){
	to = NormalizeAngle(to);
	from = NormalizeAngle(from);
	f32 result = to - from;
	if (result > PI){
		result -= 2*PI;
	}else if (result <= -PI){
		result += 2*PI;
	}
	return result;
}

// - Flips the angle horizontally.
f32 FlipAngleX(f32 angle){
	f32 result;
	angle = NormalizeAngle(angle);
	if (angle < PI){
		result = PI - angle;
	}else{
		result = PI*3.0f - angle;
	}
	return result;
}

// - Limits 'a' to range 'limit0' to 'limit1' in ascending direction.
// - If 'limit0' == 'limit1' the only result will be that limit.
// - If 'limit0' == 0 && 'limit1' == 2*PI the result can be any angle.
f32 ClampAngle(f32 a, f32 limit0, f32 limit1){
	f32 result;
	a = NormalizeAngle(a);
	
	if (limit0 == 0 && limit1 == 2*PI){ // Unlimiting range
		result = a;
	}else{
		limit0 = NormalizeAngle(limit0);
		limit1 = NormalizeAngle(limit1);

		if (limit0 <= limit1){
			if (a >= limit0 && a <= limit1){ // in range
				result = a;
			}else{ // out of range
				if (abs(AngleDifference(a, limit0)) <= (limit1-limit0)/2.0f){
					result = limit0;
				}else
					result = limit1;
			}
		}else{
			if (a >= limit0 || a <= limit1){ // in range
				result = a;
			}else{ // out of range
				if (abs(AngleDifference(a, limit0)) <= (limit1 + 2*PI - limit0)/2.0f){
					result = limit0;
				}else
					result = limit1;
			}
		}
	}
	return result;
}
