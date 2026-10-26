#!/bin/zsh
# check.sh - cross-compile the GUI designer's generated code and a project from every template,
# with the same GCC 11 the SDK has (in Docker), warnings as errors.
#   tests/templates/check.sh            all templates and the sample GUI
# Templates whose [create] runs an Amiga tool (idltool) can't be finished here: they're compiled
# as far as possible and checked fully on an OS4 machine.
set -e
here=${0:A:h}; root=${here:h:h}
out=$root/build/tcheck
rm -rf $out; mkdir -p $out
c++ -std=c++17 -O1 -Wall -I$root/src/ide -o $out/afgen $here/afgen.cpp $root/src/ide/GuiModel.cpp $root/src/ide/TemplateEngine.cpp

# the sample GUI with every kind of object
mkdir -p $out/sample
cp $here/sample.afg $here/sample_main.c $out/sample/
$out/afgen gen $out/sample/sample.afg $out/sample

# a project from each template
script=$out/build.sh
echo 'set -e; fail=0' > $script
echo 'cd /w/sample && ppc-amigaos-gcc -mcrt=newlib -O0 -gstabs -Wall -Werror -o sample sample_main.c sample_gui.c -lauto && echo "OK   sample GUI" || { echo "FAIL sample GUI"; fail=1; }' >> $script
for t in $root/templates/*/; do
  id=${t:h:t}; id=${t%/}; id=${id:t}
  $out/afgen new $root/templates $id Test$id $out/$id >/dev/null
  dir=$out/$id
  # project settings with the .afp defaults
  cflags="-O0 -gstabs -Wall"; cxxflags="-O0 -gstabs+ -Wall"; ldflags=""; libs="-lauto"; output=program
  while IFS='=' read -r k v; do
    case $k in cflags) cflags=$v;; cxxflags) cxxflags=$v;; ldflags) ldflags=$v;; libs) libs=$v;; output) output=$v;; esac
  done < $dir/project.txt
  # --- lang --- a Python template: nothing to compile; the host's python3 checks the syntax
  if grep -q '^language=python' $dir/project.txt 2>/dev/null; then
    pys=($(grep '\.py$' $dir/files.txt))
    if (cd $dir && python3 -c 'import sys; [compile(open(f).read(), f, "exec") for f in sys.argv[1:]]' $pys); then
      echo "echo 'OK   $id (Python syntax, host python3)'" >> $script
    else
      echo "echo 'FAIL $id (Python syntax)'; fail=1" >> $script
    fi
    continue
  fi
  if [ -s $dir/commands.txt ]; then
    echo "echo 'SKIP $id: runs $(head -1 $dir/commands.txt | cut -d" " -f1) when created (checked on OS4)'" >> $script
    continue
  fi
  objs=(); cmds=(); cpp=0
  for f in $(cat $dir/files.txt); do
    case $f in
      *.c) cmds+=("ppc-amigaos-gcc -mcrt=newlib $cflags -Werror -c $f -o ${f%.*}.o"); objs+=(${f%.*}.o);;
      *.cpp) cmds+=("ppc-amigaos-g++ -mcrt=newlib $cxxflags -Werror -c $f -o ${f%.*}.o"); objs+=(${f%.*}.o); cpp=1;;
    esac
  done
  if [ $cpp = 1 ]; then link="ppc-amigaos-g++ -mcrt=newlib -athread=native $ldflags -o '$output' ${objs[*]} $libs"
  else link="ppc-amigaos-gcc -mcrt=newlib $ldflags -o '$output' ${objs[*]} $libs"; fi
  extra=""
  for x in $dir/test_*.c(N); do extra="$extra && ppc-amigaos-gcc -mcrt=newlib -O0 -Wall -Werror -o ${${x:t}%.c} ${x:t} -lauto"; done
  echo "cd /w/$id && { $(printf '%s && ' $cmds) $link $extra && echo 'OK   $id'; } || { echo 'FAIL $id'; fail=1; }" >> $script
done
echo 'exit $fail' >> $script
docker run --rm -v $out:/w -w /w ${PPC_IMAGE:-walkero/amigagccondocker:os4-gcc11} sh /w/build.sh
