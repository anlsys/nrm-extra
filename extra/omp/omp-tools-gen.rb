require 'cast-to-yaml'
require 'yaml'

$parser = C::Parser::new
$parser.type_names << '__builtin_va_list'
$cpp = C::Preprocessor::new
$cpp.macros['__attribute__(a)'] = ''
$cpp.macros['__restrict'] = 'restrict'
$cpp.macros['__inline'] = 'inline'
$cpp.macros['__extension__'] = ''
$cpp.macros['__asm__(a)'] = ''
$cpp.include_path << './'

preprocessed_sources_libc = $cpp.preprocess(<<EOF).gsub(/^#.*?$/, '')
#include <stdint.h>
#include <stddef.h>
EOF

$parser.parse(preprocessed_sources_libc)

preprocessed_sources_omp_tools_api = $cpp.preprocess(<<EOF).gsub(/^#.*?$/, '')
#include <omp-tools.h>
EOF

ast = $parser.parse(preprocessed_sources_omp_tools_api)

File::open("omp-tools.yaml", "w") { |f|
  f.puts ast.extract_declarations.to_yaml
}

require_relative 'yaml_ast'

$omp_tools_yaml = YAML::load_file("omp-tools.yaml")
$omp_tools_api = YAMLCAst.from_yaml_ast($omp_tools_yaml)

$omp_tools_types = $omp_tools_api["typedefs"]
$omp_tools_enums = $omp_tools_api["enums"]

enum_callbacks = $omp_tools_enums.find { |e| e.name == "ompt_callbacks_t" }

omp_tools_events_mandatory = YAML::load_file("omp-tools-events.yaml")

callback_names = enum_callbacks.members.collect { |m| m.name + "_t" }

callbacks = $omp_tools_types.select { |t|
  callback_names.include?(t.name)
}.collect { |t|
  [t.name.sub(/_t\z/, ""), ["nrm_" + t.name.sub(/_t\z/, "_cb"), t] ]
}.to_h

callbacks.each do |_, (f_name, t)|
  puts t.type.type.to_s(f_name)
  puts <<EOF
{
	nrm_send_progress(ctxt, 1);
}

EOF
end


puts <<EOF
void nrm_ompt_register_cbs(void)
{

EOF
callbacks.each do |e, (f_name, _)|
  puts <<EOF
	ret = nrm_ompt_set_callback(
		#{e},
		(ompt_callback_t)#{f_name});
EOF

  if omp_tools_events_mandatory[e]
    puts <<EOF
	assert(ret == ompt_set_always);

EOF
  else
    puts
  end

end

puts "}"






