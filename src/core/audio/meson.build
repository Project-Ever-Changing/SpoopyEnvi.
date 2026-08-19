# Taisei Project's audio system is simple to understand and implement
# Taken from: https://github.com/taisei-project/taisei/blob/master/src/audio/meson.build

audio_backend_opts = {
	'sdl' :  get_option('audio_sdl'),
	'null' : get_option('audio_null'),
}

default_audio_backend = get_option('audio_default')
audio_backends_prio = ['sdl', 'null']

if default_audio_backend == 'auto'
	foreach audio_backend : audio_backends_prio
		if audio_backend_opts[audio_backend].allowed()
			default_audio_backend = audio_backend
			break
		endif
	endforeach

	if default_audio_backend == 'auto'
		audio_backends_prio_opts = []
		foreach audio_backend : audio_backends_prio
			audio_backends_prio_opts += f'sp_aud_@audio_backend@'
		endforeach

		audio_backends_prio_opts = ', '.join(audio_backends_prio_opts)
		error('Unable to pick out a default audio backend' +
			f'Please make sure to have a least one of the audio_backends_prio_opts enabled.')
	endif
endif

assert(audio_backend_opts[default_audio_backend].allowed(),
	f'The default audio backend @default_audio_backend@ is not enabled')

spoopy_audio_src = files(
	'spoopy_audio.c',
	'spoopy_audio_backend.c',
	'spoopy_stream.c',
	'spoopy_player.c',
	'spoopy_mixer.c',
)

spoopy_audio_deps = []
audio_macro = []
enabled_audio_backends = []

foreach audio_backend, aopt : audio_backend_opts
	if aopt.allowed()
		subdir(audio_backend)
		audio_macro += ['AUDIO(@0@)'.format(audio_backend)]
		enabled_audio_backends += audio_backend

		spoopy_audio_src += get_variable(f'audio_@audio_backend@_src')
	endif
endforeach

subdir('files')
spoopy_audio_src += audio_files_src

audio_macro = ' '.join(audio_macro)
config.set('SPOOPY_HAS_AUDIO_BACKENDS', audio_macro)
config.set_quoted('SPOOPY_HAS_AUDIO_DEFAULT', default_audio_backend)
