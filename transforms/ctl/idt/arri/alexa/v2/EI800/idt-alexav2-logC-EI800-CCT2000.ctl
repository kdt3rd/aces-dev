
// ARRI ALEXA IDT for ALEXA logC files
//  with camera EI set to 800
//  and CCT of adopted white set to 2000K
// Written by v2_IDT_maker.py v0.05 on Saturday 10 March 2012 by josephgoldstone

float
normalizedLogC2ToRelativeExposure(float x) {
	if (x > 0.131313)
		return (pow(10,(x - 0.391007) / 0.247189) - 0.089004) / 5.061087;
	else
		return (x - 0.131313) / 4.950469;
}

void main
(	input varying float rIn,
	input varying float gIn,
	input varying float bIn,
	input varying float aIn,
	output varying float rOut,
	output varying float gOut,
	output varying float bOut,
	output varying float aOut)
{

	float r_lin = normalizedLogC2ToRelativeExposure(rIn);
	float g_lin = normalizedLogC2ToRelativeExposure(gIn);
	float b_lin = normalizedLogC2ToRelativeExposure(bIn);

	rOut = r_lin * 0.795933 + g_lin * 0.042336 + b_lin * 0.161731;
	gOut = r_lin * -0.019505 + g_lin * 1.075681 + b_lin * -0.056176;
	bOut = r_lin * 0.013338 + g_lin * -0.387268 + b_lin * 1.373929;
	aOut = 1.0;

}
