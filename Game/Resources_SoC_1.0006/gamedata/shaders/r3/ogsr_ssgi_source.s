local function setup_source(shader, pixel_shader)
	shader:begin("stub_screen_space", pixel_shader)
		:fog(false)
		:zb(false, false)
		:blend(true, blend.one, blend.one)
	shader:dx10texture("s_position", "$user$position")
	shader:dx10texture("s_diffuse", "$user$albedo")
	shader:dx10texture("s_ao", "$user$ao")
	shader:dx10texture("env_s0", "$user$env_s0")
	shader:dx10texture("env_s1", "$user$env_s1")
	jitter.jitter(shader)
	shader:dx10sampler("smp_base")
	shader:dx10sampler("smp_nofilter")
	shader:dx10sampler("smp_material")
	shader:dx10sampler("smp_rtlinear")
end

function element_0(shader, t_base, t_second, t_detail)
	setup_source(shader, "ogsr_ssgi_source")
end

function element_1(shader, t_base, t_second, t_detail)
	setup_source(shader, "ogsr_ssgi_source_ao")
end
