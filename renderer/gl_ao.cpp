/*
* Descent 3MASTERED
* Copyright (C) 2024 SaladBadger
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "gl_local.h"
#include "gameloop.h"
#include <math.h>
#include <stdint.h>

namespace
{
	//Mersenne-Twister-derived noise values. Two channels:
	//(cos-of-angle-ish, jitter).
	const float kMtNoise[32] =
	{
		0.556725f, 0.005520f, 0.708315f, 0.583199f, 0.236644f, 0.992380f, 0.981091f, 0.119804f,
		0.510866f, 0.560499f, 0.961497f, 0.557862f, 0.539955f, 0.332871f, 0.417807f, 0.920779f,
		0.730747f, 0.076690f, 0.008562f, 0.660104f, 0.428921f, 0.511342f, 0.587871f, 0.906406f,
		0.437980f, 0.620309f, 0.062196f, 0.119485f, 0.235646f, 0.795892f, 0.044437f, 0.617311f,
	};

	constexpr int AO_DEFAULT_SAMPLES = 128;

	void AOSamplePattern(int samples, int* directions_out, int* steps_out)
	{
		if (samples < 1)
			samples = AO_DEFAULT_SAMPLES;

		int directions;
		if (samples <= 12)
		{
			directions = samples < 4 ? samples : 4;
		}
		else if (samples <= 24)
		{
			directions = 6;
		}
		else if (samples <= 32)
		{
			directions = 8;
		}
		else
		{
			directions = (int)(sqrtf((float)samples * 2.0f) + 0.5f);
			if (directions < 8)
				directions = 8;
			if (directions > 32)
				directions = 32;
		}

		int steps = (samples + directions - 1) / directions;
		if (steps < 1)
			steps = 1;

		*directions_out = directions;
		*steps_out = steps;
	}

	float Clamp01(float value)
	{
		if (value < 0.0f)
			return 0.0f;
		if (value > 1.0f)
			return 1.0f;
		return value;
	}

	int ClampInt(int value, int min_value, int max_value)
	{
		if (value < min_value)
			return min_value;
		if (value > max_value)
			return max_value;
		return value;
	}

	float ClampFloat(float value, float min_value, float max_value)
	{
		if (value < min_value)
			return min_value;
		if (value > max_value)
			return max_value;
		return value;
	}

	int AOResolutionScale(const renderer_preferred_state& pref_state, int width, int height)
	{
		switch (pref_state.ao_resolution)
		{
		case AO_RESOLUTION_FULL:
			return 1;
		case AO_RESOLUTION_HALF:
			return 2;
		case AO_RESOLUTION_QUARTER:
			return 4;
		case AO_RESOLUTION_AUTO:
		default:
			break;
		}

		//Start at scale 1 and double until the AO buffer fits the budget.
		//Seeding with SupersamplingFactor was redundant - the loop below
		//catches SSAA-inflated sources too - and worse, it forced unnecessary
		//downsampling on small back buffers when SSAA was high.
		int scale = 1;
		int target_pixels = 1920 * 1080;
		//MSAA-resolved depth still costs sample-count bandwidth per AO pixel
		//in upstream resolves; shrink the budget so we trade a bit of AO
		//resolution for fewer per-sample reads.
		int samples = (int)RendererMsaaSamples(pref_state);
		if (samples >= 4)
			target_pixels /= 2;
		while (scale < 4)
		{
			int ao_width = (width + scale - 1) / scale;
			int ao_height = (height + scale - 1) / scale;
			if (ao_width * ao_height <= target_pixels)
				break;
			scale *= 2;
		}
		return scale;
	}

	bool FramebufferLargerThan(const ColorFramebuffer& framebuffer, int width, int height)
	{
		return framebuffer.Handle() != 0 &&
			(framebuffer.Width() > (uint32_t)width || framebuffer.Height() > (uint32_t)height);
	}

	void AODrawFullscreen(GLuint framebuffer, int width, int height)
	{
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glViewport(0, 0, width, height);
		glBindVertexArray(GL_GetFramebufferVAO());
		rend_RecordDrawCall(RENDERER_DRAW_CALL_POSTPROCESS);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
	}

}

void AOResources::InitShaders()
{
	extern const char* blitVertexSrc;
	extern const char* aoDepthFragmentSrc;
	extern const char* aoAOFragmentSrc;
	extern const char* aoBlurFragmentSrc;
	extern const char* aoTemporalFragmentSrc;

	depth_shader.AttachSource(blitVertexSrc, aoDepthFragmentSrc);
	depth_shader.Use();
	depth_source = depth_shader.FindUniform("depth_tex");
	depth_source_ms = depth_shader.FindUniform("depth_ms_tex");
	depth_ao_class = depth_shader.FindUniform("ao_class_tex");
	if (depth_source != -1) glUniform1i(depth_source, 0);
	if (depth_source_ms != -1) glUniform1i(depth_source_ms, 1);
	if (depth_ao_class != -1) glUniform1i(depth_ao_class, 2);
	depth_has_ao_class = depth_shader.FindUniform("has_ao_class");
	depth_ao_weight_is_direct = depth_shader.FindUniform("ao_weight_is_direct");
	depth_samples = depth_shader.FindUniform("depth_samples");
	depth_input_screen_size = depth_shader.FindUniform("input_screen_size");
	depth_ao_screen_size = depth_shader.FindUniform("ao_screen_size");
	depth_projection = depth_shader.FindUniform("depth_projection");
	depth_terrain_occlusion = depth_shader.FindUniform("terrain_ao_occlusion");
	depth_polyobject_occlusion = depth_shader.FindUniform("polyobject_ao_occlusion");
	depth_mine_rock_occlusion = depth_shader.FindUniform("mine_rock_ao_occlusion");
	depth_mine_occlusion = depth_shader.FindUniform("mine_ao_occlusion");

	ao_shader.AttachSource(blitVertexSrc, aoAOFragmentSrc);
	ao_shader.Use();
	GLint depth_loc = ao_shader.FindUniform("depth_tex");
	GLint noise_loc = ao_shader.FindUniform("noise_tex");
	if (depth_loc != -1) glUniform1i(depth_loc, 0);
	if (noise_loc != -1) glUniform1i(noise_loc, 1);
	ao_proj_info = ao_shader.FindUniform("proj_info");
	ao_near_far = ao_shader.FindUniform("near_far");
	ao_radius = ao_shader.FindUniform("radius");
	ao_radius_pixels = ao_shader.FindUniform("radius_pixels");
	ao_max_radius_pixels = ao_shader.FindUniform("max_radius_pixels");
	ao_neg_inv_radius2 = ao_shader.FindUniform("neg_inv_radius2");
	ao_angle_bias = ao_shader.FindUniform("angle_bias");
	ao_inv_screen_size = ao_shader.FindUniform("inv_screen_size");
	ao_working_inv_screen_size = ao_shader.FindUniform("ao_inv_screen_size");
	ao_screen_size = ao_shader.FindUniform("screen_size");
	ao_noise_origin = ao_shader.FindUniform("noise_origin");
	ao_noise_jitter = ao_shader.FindUniform("noise_jitter");
	ao_directions = ao_shader.FindUniform("directions");
	ao_steps = ao_shader.FindUniform("steps");
	ao_terrain_occlusion = ao_shader.FindUniform("terrain_ao_occlusion");
	ao_polyobject_occlusion = ao_shader.FindUniform("polyobject_ao_occlusion");
	ao_mine_rock_occlusion = ao_shader.FindUniform("mine_rock_ao_occlusion");
	ao_mine_occlusion = ao_shader.FindUniform("mine_ao_occlusion");

	blur_x_shader.AttachSource(blitVertexSrc, aoBlurFragmentSrc);
	blur_x_shader.Use();
	GLint blur_x_heh = blur_x_shader.FindUniform("heh");
	if (blur_x_heh != -1) glUniform1i(blur_x_heh, 0);
	blur_x_delta = blur_x_shader.FindUniform("blur_delta");
	blur_x_sharpness = blur_x_shader.FindUniform("sharpness");
	blur_x_radius = blur_x_shader.FindUniform("kernel_radius");

	blur_y_shader.AttachSource(blitVertexSrc, aoBlurFragmentSrc);
	blur_y_shader.Use();
	GLint blur_y_heh = blur_y_shader.FindUniform("heh");
	if (blur_y_heh != -1) glUniform1i(blur_y_heh, 0);
	blur_y_delta = blur_y_shader.FindUniform("blur_delta");
	blur_y_sharpness = blur_y_shader.FindUniform("sharpness");
	blur_y_radius = blur_y_shader.FindUniform("kernel_radius");

	temporal_shader.AttachSource(blitVertexSrc, aoTemporalFragmentSrc);
	temporal_shader.Use();
	temporal_current_ao = temporal_shader.FindUniform("current_ao");
	temporal_history_ao = temporal_shader.FindUniform("history_ao");
	temporal_velocity_source = temporal_shader.FindUniform("velocity_source");
	temporal_depth_source = temporal_shader.FindUniform("depth_source");
	temporal_object_id_source = temporal_shader.FindUniform("object_id_source");
	if (temporal_current_ao != -1) glUniform1i(temporal_current_ao, 0);
	if (temporal_history_ao != -1) glUniform1i(temporal_history_ao, 1);
	if (temporal_velocity_source != -1) glUniform1i(temporal_velocity_source, 2);
	if (temporal_depth_source != -1) glUniform1i(temporal_depth_source, 3);
	if (temporal_object_id_source != -1) glUniform1i(temporal_object_id_source, 4);
	temporal_history_valid = temporal_shader.FindUniform("history_valid");
	temporal_has_dynamic_velocity = temporal_shader.FindUniform("has_dynamic_velocity");
	temporal_has_static_reconstruction = temporal_shader.FindUniform("has_static_reconstruction");
	temporal_history_blend = temporal_shader.FindUniform("history_blend");
	temporal_depth_reject = temporal_shader.FindUniform("depth_reject");
	temporal_velocity_reject = temporal_shader.FindUniform("velocity_reject_pixels");
	temporal_source_size = temporal_shader.FindUniform("source_size");
	temporal_velocity_uv_scale = temporal_shader.FindUniform("velocity_uv_scale");
	temporal_current_projection = temporal_shader.FindUniform("current_projection");
	temporal_current_inverse_modelview = temporal_shader.FindUniform("current_inverse_modelview");
	temporal_previous_view_projection = temporal_shader.FindUniform("previous_view_projection");



	ShaderProgram::ClearBinding();

	//Build the 4x4 noise texture. Each texel holds two random scalars used
	//for rotating the AO sampling directions and for the per-pixel ray jitter.
	if (noise_texture == 0)
	{
		glGenTextures(1, &noise_texture);
		glBindTexture(GL_TEXTURE_2D, noise_texture);
		uint8_t pixels[4 * 4 * 2];
		for (int i = 0; i < 16; i++)
		{
			float r = kMtNoise[(i * 2) % 32];
			float g = kMtNoise[(i * 2 + 1) % 32];
			pixels[i * 2 + 0] = (uint8_t)(r * 255.0f);
			pixels[i * 2 + 1] = (uint8_t)(g * 255.0f);
		}
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, 4, 4, 0, GL_RG, GL_UNSIGNED_BYTE, pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

void AOResources::DestroyShaders()
{
	depth_shader.Destroy();
	ao_shader.Destroy();
	blur_x_shader.Destroy();
	blur_y_shader.Destroy();
	temporal_shader.Destroy();
	if (noise_texture)
	{
		glDeleteTextures(1, &noise_texture);
		noise_texture = 0;
	}
}

bool AOResources::HasFramebuffers() const
{
	return ao_depth_framebuffer.Handle() != 0 ||
		ao_framebuffer.Handle() != 0 ||
		ao_blur_framebuffer.Handle() != 0 ||
		ao_history_framebuffers[0].Handle() != 0 ||
		ao_history_framebuffers[1].Handle() != 0;
}

void AOResources::DestroyFramebuffers()
{
	ao_depth_framebuffer.Destroy();
	ao_framebuffer.Destroy();
	ao_blur_framebuffer.Destroy();
	ao_history_framebuffers[0].Destroy();
	ao_history_framebuffers[1].Destroy();
	ao_history_index = 0;
	ao_history_valid = false;
}

void AOResources::Destroy()
{
	DestroyFramebuffers();
	DestroyShaders();
}

AOResult AOResources::Evaluate(const AOInputs& inputs, const renderer_preferred_state& pref_state)
{
	AOResult result{};
	if (!pref_state.ao_enabled)
	{
		if (HasFramebuffers())
		{
			glFinish();
			DestroyFramebuffers();
		}
		return result;
	}

	PERF_MARKER_SCOPE("AO.Total");
	if (!inputs.depth || !inputs.surface_records || !inputs.object_ids || inputs.width <= 0 || inputs.height <= 0 ||
		inputs.raster_width <= 0 || inputs.raster_height <= 0 || inputs.raster_scale < 1 ||
		inputs.viewport.width <= 0 || inputs.viewport.height <= 0)
		Error("AO requires resolved opaque depth, surface records and an explicit image extent");
	const int source_width = inputs.width, source_height = inputs.height;
	const int noise_origin_x = inputs.viewport.x, noise_origin_y = inputs.viewport.y;
	const GLuint velocity_texture = inputs.surface_records, object_id_texture = inputs.object_ids;
	const GLuint ao_weight_texture = inputs.surface_records;
	const bool ao_weight_is_direct = false;
	const int velocity_width = inputs.raster_width, velocity_height = inputs.raster_height;
	const int velocity_scale = inputs.raster_scale;
	const auto* projection = inputs.current.projection.data();
	const auto* current_projection = projection;
	const auto* current_inverse_modelview = inputs.current.inverse_view.data();
	const auto* previous_view_projection = inputs.previous.view_projection.data();
	const bool has_static_reconstruction = inputs.moving && inputs.previous_valid;
	const bool has_dynamic_velocity = inputs.moving;
	const bool reset_temporal_history = inputs.reset_history;
	const float nearz = fabsf(projection[14]/(-projection[11]-projection[10]));
	const float far_denominator = projection[11]-projection[10];
	const float farz = fabsf(far_denominator) > 1.e-6f ? fabsf(projection[14]/far_denominator) : 10000.f;

	int ao_scale = AOResolutionScale(pref_state, source_width, source_height);
	int ao_width = (source_width + ao_scale - 1) / ao_scale;
	int ao_height = (source_height + ao_scale - 1) / ao_scale;
	if (ao_width <= 0) ao_width = 1;
	if (ao_height <= 0) ao_height = 1;

	if (FramebufferLargerThan(ao_depth_framebuffer, ao_width, ao_height) ||
		FramebufferLargerThan(ao_framebuffer, ao_width, ao_height) ||
		FramebufferLargerThan(ao_blur_framebuffer, ao_width, ao_height))
	{
		glFinish();
		DestroyFramebuffers();
	}

	// The opaque export is resolved at logical resolution. AO may reduce it
	// further; its kernel never scales up to the SSAA surface-record extent.
	ao_depth_framebuffer.Update(ao_width, ao_height, GL_RG32F, GL_RG, GL_FLOAT);
	//These buffers store AO plus linear depth; half-float depth avoids
	//depth-edge ghosts from 8-bit quantization in the bilateral blur.
	ao_framebuffer.Update(ao_width, ao_height, GL_RG16F, GL_RG, GL_FLOAT);
	ao_blur_framebuffer.Update(ao_width, ao_height, GL_RG16F, GL_RG, GL_FLOAT);

	//Save GL state we are about to trample so we leave the caller in
	//the same state we found it in (matters for follow-up draw calls).
	GLint old_read = 0, old_draw = 0;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw);
	GLint old_viewport[4];
	glGetIntegerv(GL_VIEWPORT, old_viewport);
	GLboolean blend_enabled = glIsEnabled(GL_BLEND);
	GLboolean depth_enabled = glIsEnabled(GL_DEPTH_TEST);
	GLboolean cull_enabled = glIsEnabled(GL_CULL_FACE);
	GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean multisample_enabled = glIsEnabled(GL_MULTISAMPLE);
	GLint blend_src_rgb = GL_ONE, blend_dst_rgb = GL_ZERO;
	GLint blend_src_alpha = GL_ONE, blend_dst_alpha = GL_ZERO;
	glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
	glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha);
	GLboolean depth_mask = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
	GLboolean color_mask[4];
	glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);

	const GLuint depth_texture = inputs.depth;
	//AO reads resolved 2D inputs. Hardware resolves are much cheaper than
	//doing source-block * sample-count loops in the AO/suppression shaders.
	int input_samples = 0;

	//Compute uniform values.
	float m00 = projection[0];
	float m11 = projection[5];
	if (fabsf(m00) < 1e-6f) m00 = 1.0f;
	if (fabsf(m11) < 1e-6f) m11 = 1.0f;

	int samples = ClampInt((int)pref_state.ao_sample_count, 1, 1024);
	int directions = 0;
	int steps = 0;
	AOSamplePattern(samples, &directions, &steps);

	float radius = ClampFloat(pref_state.ao_radius, 0.1f, 12.0f);
	float bias = ClampFloat(pref_state.ao_bias, 0.0f, 1.0f);

	//Convert world-space radius to a pixel-space scale at depth==1. The shader
	//then divides by per-pixel depth to get the on-screen step size. This is
	//Equivalent to: r_pixels = radius * 0.5 * (h / tan(fov/2)).
	//Since m11 = 1 / tan(half_y_fov_with_aspect_compensation), pixel size is:
	float radius_pixels = radius * 0.5f * (float)ao_height * m11;
	const int visible_width = inputs.viewport.width;
	const int visible_height = inputs.viewport.height;
	const int visible_ao_width = (visible_width + ao_scale - 1) / ao_scale;
	const int visible_ao_height = (visible_height + ao_scale - 1) / ao_scale;
	float max_radius_pixels = 128.0f * sqrtf((float)(visible_ao_width * visible_ao_height) / (1080.0f * 1920.0f));
	if (max_radius_pixels < 16.0f) max_radius_pixels = 16.0f;

	float neg_inv_r2 = -1.0f / (radius * radius);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_MULTISAMPLE);
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	//-------------------------------------------------------------------------
	// Pass 1: downsample/resolve source depth into AO resolution.
	//-------------------------------------------------------------------------
	{
		PERF_MARKER_SCOPE("AO.DepthReduce");
		depth_shader.Use();
		if (depth_samples != -1)
			glUniform1i(depth_samples, input_samples);
		if (depth_has_ao_class != -1)
			glUniform1i(depth_has_ao_class, ao_weight_texture != 0 ? 1 : 0);
		if (depth_ao_weight_is_direct != -1)
			glUniform1i(depth_ao_weight_is_direct, ao_weight_is_direct ? 1 : 0);
		if (depth_input_screen_size != -1)
			glUniform2f(depth_input_screen_size, (float)source_width, (float)source_height);
		if (depth_ao_screen_size != -1)
			glUniform2f(depth_ao_screen_size, (float)ao_width, (float)ao_height);
		if (depth_projection != -1)
			glUniform4f(depth_projection, projection[14], projection[10], projection[11], farz);
		if (depth_terrain_occlusion != -1)
			glUniform1f(depth_terrain_occlusion, Clamp01(pref_state.ao_terrain_occlusion));
		if (depth_polyobject_occlusion != -1)
			glUniform1f(depth_polyobject_occlusion, Clamp01(pref_state.ao_polyobject_occlusion));
		if (depth_mine_rock_occlusion != -1)
			glUniform1f(depth_mine_rock_occlusion, Clamp01(pref_state.ao_mine_rock_occlusion));
		if (depth_mine_occlusion != -1)
			glUniform1f(depth_mine_occlusion, Clamp01(pref_state.ao_mine_occlusion));

		rend_ClearBoundTextures();
		GL_BindFramebufferTexture(depth_texture, 0, GL_NEAREST);
		if (ao_weight_texture != 0)
			GL_BindFramebufferTexture(ao_weight_texture, 2, GL_NEAREST);
		AODrawFullscreen(ao_depth_framebuffer.Handle(), ao_width, ao_height);
		GL4PerfGpuAOMark(GL4_GPU_AO_AFTER_DEPTH_REDUCE);
	}

	//-------------------------------------------------------------------------
	// Pass 2: AO into ao_framebuffer.
	//-------------------------------------------------------------------------
	{
		PERF_MARKER_SCOPE("AO.Generate");
		ao_shader.Use();
		glUniform4f(ao_proj_info,
			2.0f / m00,
			2.0f / m11,
			(-projection[8] / projection[11] - 1.0f) / m00,
			(-projection[9] / projection[11] - 1.0f) / m11);
		glUniform2f(ao_near_far, nearz, farz);
		glUniform1f(ao_radius, radius);
		glUniform1f(ao_radius_pixels, radius_pixels);
		glUniform1f(ao_max_radius_pixels, max_radius_pixels);
		glUniform1f(ao_neg_inv_radius2, neg_inv_r2);
		glUniform1f(ao_angle_bias, bias);
		glUniform2f(ao_inv_screen_size, 1.0f / (float)ao_width, 1.0f / (float)ao_height);
		if (ao_working_inv_screen_size != -1)
			glUniform2f(ao_working_inv_screen_size, 1.0f / (float)ao_width, 1.0f / (float)ao_height);
		glUniform2f(ao_screen_size, (float)ao_width, (float)ao_height);
		if (ao_noise_origin != -1)
		{
			glUniform2f(ao_noise_origin,
				(float)noise_origin_x * (float)ao_width / (float)source_width,
				(float)noise_origin_y * (float)ao_height / (float)source_height);
		}
		if (ao_noise_jitter != -1)
		{
			float jitter_x = 0.0f;
			float jitter_y = 0.0f;
			if (!reset_temporal_history)
			{
				const unsigned int frame = ao_jitter_frame++;
				jitter_x = fmodf((float)(frame + 1u) * 0.754877666f, 1.0f);
				jitter_y = fmodf((float)(frame + 1u) * 0.569840291f, 1.0f);
			}
			else
			{
				ao_jitter_frame = 0;
			}
			glUniform2f(ao_noise_jitter, jitter_x, jitter_y);
		}
		glUniform1i(ao_directions, directions);
		glUniform1i(ao_steps, steps);
		if (ao_terrain_occlusion != -1)
			glUniform1f(ao_terrain_occlusion, Clamp01(pref_state.ao_terrain_occlusion));
		if (ao_polyobject_occlusion != -1)
			glUniform1f(ao_polyobject_occlusion, Clamp01(pref_state.ao_polyobject_occlusion));
		if (ao_mine_rock_occlusion != -1)
			glUniform1f(ao_mine_rock_occlusion, Clamp01(pref_state.ao_mine_rock_occlusion));
		if (ao_mine_occlusion != -1)
			glUniform1f(ao_mine_occlusion, Clamp01(pref_state.ao_mine_occlusion));

		rend_ClearBoundTextures();
		GL_BindFramebufferTexture(ao_depth_framebuffer.ColorTextureForRead(), 0, GL_NEAREST);
		GL_BindFramebufferTexture(noise_texture, 1, GL_NEAREST);
		AODrawFullscreen(ao_framebuffer.Handle(), ao_width, ao_height);
		GL4PerfGpuAOMark(GL4_GPU_AO_AFTER_GENERATE);
	}

	//-------------------------------------------------------------------------
	// Pass 3: Optional separable bilateral blur (X then Y).
	//-------------------------------------------------------------------------
	int blur_radius = ClampInt((int)pref_state.ao_blur_radius, 0, 20);
	ColorFramebuffer* blurred = &ao_framebuffer;

	if (blur_radius > 0)
	{
		//AO shader outputs depth as P.z / farz, so .y lives in [0, 1].
		//The blur weight = exp2(-r^2/(2*sigma^2) - (sharpness * dz)^2).
		//Scale sharpness from radius so blur smooths within surfaces without
		//pulling dark occlusion lines across nearby geometry.
		float sharpness = farz / (radius * 4.0f);
		if (sharpness < 180.0f) sharpness = 180.0f;
		if (sharpness > 1400.0f) sharpness = 1400.0f;

		{
			PERF_MARKER_SCOPE("AO.BlurX");
			blur_x_shader.Use();
			glUniform2f(blur_x_delta, 1.0f / (float)ao_width, 0.0f);
			glUniform1f(blur_x_sharpness, sharpness);
			glUniform1i(blur_x_radius, blur_radius);
			rend_ClearBoundTextures();
			GL_BindFramebufferTexture(ao_framebuffer.ColorTextureForRead(), 0, GL_NEAREST);
			AODrawFullscreen(ao_blur_framebuffer.Handle(), ao_width, ao_height);
			GL4PerfGpuAOMark(GL4_GPU_AO_AFTER_BLUR_X);
		}

		{
			PERF_MARKER_SCOPE("AO.BlurY");
			blur_y_shader.Use();
			glUniform2f(blur_y_delta, 0.0f, 1.0f / (float)ao_height);
			glUniform1f(blur_y_sharpness, sharpness);
			glUniform1i(blur_y_radius, blur_radius);
			rend_ClearBoundTextures();
			GL_BindFramebufferTexture(ao_blur_framebuffer.ColorTextureForRead(), 0, GL_NEAREST);
			AODrawFullscreen(ao_framebuffer.Handle(), ao_width, ao_height);
			GL4PerfGpuAOMark(GL4_GPU_AO_AFTER_BLUR_Y);
		}

		blurred = &ao_framebuffer;
	}

	ColorFramebuffer* apply_source = blurred;

	//-------------------------------------------------------------------------
	// Pass 4: Optional temporal reprojection/accumulation.
	//-------------------------------------------------------------------------
	if (reset_temporal_history)
		ao_history_valid = false;

	const float temporal_blend = ClampFloat(pref_state.ao_temporal_blend, 0.0f, 0.995f);
	const bool temporal_debug = pref_state.ao_temporal_debug_preview;
	const bool use_temporal = temporal_shader.Handle() != 0 && (temporal_blend > 0.0f || temporal_debug);
	if (use_temporal)
	{
		const bool history_size_changed =
			ao_history_framebuffers[0].Handle() == 0 ||
			ao_history_framebuffers[1].Handle() == 0 ||
			ao_history_framebuffers[0].Width() != (uint32_t)ao_width ||
			ao_history_framebuffers[0].Height() != (uint32_t)ao_height ||
			ao_history_framebuffers[1].Width() != (uint32_t)ao_width ||
			ao_history_framebuffers[1].Height() != (uint32_t)ao_height;
		if (history_size_changed)
		{
			ao_history_valid = false;
			ao_history_index = 0;
		}
		ao_history_framebuffers[0].Update(ao_width, ao_height, GL_RGBA16F, GL_RGBA, GL_FLOAT);
		ao_history_framebuffers[1].Update(ao_width, ao_height, GL_RGBA16F, GL_RGBA, GL_FLOAT);

		int history_read = ao_history_index;
		int history_write = 1 - history_read;
		ColorFramebuffer* history_source = ao_history_valid ?
			&ao_history_framebuffers[history_read] : apply_source;
		ColorFramebuffer* temporal_target = &ao_history_framebuffers[history_write];

		const bool use_dynamic_velocity =
			has_dynamic_velocity && velocity_texture != 0 && object_id_texture != 0 &&
			velocity_width > 0 && velocity_height > 0;
		const bool use_static_velocity =
			has_static_reconstruction && depth_texture != 0 &&
			current_projection != nullptr && current_inverse_modelview != nullptr &&
			previous_view_projection != nullptr;
		const float velocity_uv_scale_x = velocity_width > 0 ?
			(float)(source_width * velocity_scale) / (float)velocity_width : 1.0f;
		const float velocity_uv_scale_y = velocity_height > 0 ?
			(float)(source_height * velocity_scale) / (float)velocity_height : 1.0f;

		PERF_MARKER_SCOPE("AO.Temporal");
		temporal_shader.Use();
		if (temporal_history_valid != -1)
			glUniform1i(temporal_history_valid, ao_history_valid ? 1 : 0);
		if (temporal_has_dynamic_velocity != -1)
			glUniform1i(temporal_has_dynamic_velocity, use_dynamic_velocity ? 1 : 0);
		if (temporal_has_static_reconstruction != -1)
			glUniform1i(temporal_has_static_reconstruction, use_static_velocity ? 1 : 0);
		if (temporal_history_blend != -1)
			glUniform1f(temporal_history_blend, temporal_blend);
		if (temporal_depth_reject != -1)
			glUniform1f(temporal_depth_reject,
				ClampFloat(pref_state.ao_temporal_depth_reject, 0.0f, 0.10f));
		if (temporal_velocity_reject != -1)
			glUniform1f(temporal_velocity_reject,
				ClampFloat(pref_state.ao_temporal_velocity_reject, 0.0f, 1024.0f));
		if (temporal_source_size != -1)
			glUniform2f(temporal_source_size, (float)source_width, (float)source_height);
		if (temporal_velocity_uv_scale != -1)
			glUniform2f(temporal_velocity_uv_scale, velocity_uv_scale_x, velocity_uv_scale_y);
		if (use_static_velocity)
		{
			if (temporal_current_projection != -1)
				glUniformMatrix4fv(temporal_current_projection, 1, GL_FALSE, current_projection);
			if (temporal_current_inverse_modelview != -1)
				glUniformMatrix4fv(temporal_current_inverse_modelview, 1, GL_FALSE,
					current_inverse_modelview);
			if (temporal_previous_view_projection != -1)
				glUniformMatrix4fv(temporal_previous_view_projection, 1, GL_FALSE,
					previous_view_projection);
		}

		rend_ClearBoundTextures();
		GL_BindFramebufferTexture(apply_source->ColorTextureForRead(), 0, GL_NEAREST);
		GL_BindFramebufferTexture(history_source->ColorTextureForRead(), 1, GL_LINEAR);
		if (use_dynamic_velocity)
		{
			GL_BindFramebufferTexture(velocity_texture, 2, GL_NEAREST);
			GL_BindFramebufferTexture(object_id_texture, 4, GL_NEAREST);
		}
		if (use_static_velocity)
			GL_BindFramebufferTexture(depth_texture, 3, GL_NEAREST);

		AODrawFullscreen(temporal_target->Handle(), ao_width, ao_height);
		GL4PerfGpuAOMark(GL4_GPU_AO_AFTER_TEMPORAL);
		apply_source = temporal_target;
		ao_history_index = history_write;
		ao_history_valid = true;
	}
	else
	{
		ao_history_valid = false;
	}

	//-------------------------------------------------------------------------
	// Publish the estimate; material lighting owns eligibility/highlights.
	//-------------------------------------------------------------------------
	const bool debug_display_ao_only =
		(pref_state.ao_debug_preview || pref_state.ao_temporal_debug_preview);
	result.ao_texture = apply_source->ColorTextureForRead();
	result.debug_display = debug_display_ao_only;
	result.debug_channel = pref_state.ao_temporal_debug_preview ? 2 : 0;


	//Restore prior GL state.
	glBlendFuncSeparate(blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha);
	if (!blend_enabled) glDisable(GL_BLEND); else glEnable(GL_BLEND);
	if (depth_enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (cull_enabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (scissor_enabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
	if (multisample_enabled) glEnable(GL_MULTISAMPLE); else glDisable(GL_MULTISAMPLE);
	glDepthMask(depth_mask);
	glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);

	ShaderProgram::ClearBinding();

	glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_draw);
	glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);

	rend_ClearBoundTextures();
	rend_RestoreLegacy();
	return result;
}
