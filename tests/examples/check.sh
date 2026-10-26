#!/bin/zsh
# check.sh - cross-compile every example project in examples/ the way AmiForge builds it (the
# flags from its .afp, -athread=native for C++), with the SDK's GCC 11 in Docker and warnings
# as errors.
#   tests/examples/check.sh            all examples
#   tests/examples/check.sh clock todo just these
set -e
here=${0:A:h}; root=${here:h:h}
out=$root/build/excheck
rm -rf $out; mkdir -p $out
names=($@)
[[ ${#names} -gt 0 ]] || names=($root/examples/*(/:t))
script=$out/build.sh
echo 'set -e; fail=0' > $script
for ex in $names; do
  src=$root/examples/$ex
  afp=($src/*.afp(N))
  [[ ${#afp} -eq 1 ]] || { print -u2 "check: examples/$ex should have one .afp"; exit 1 }
  cp -R $src $out/$ex
  rm -rf $out/$ex/obj
  # the .afp's settings over AmiForge's defaults
  cflags="-O0 -gstabs -Wall"; cxxflags="-O0 -gstabs+ -Wall"; asflags="-gstabs"; ldflags=""; libs="-lauto"; output=program
  files=()
  section=""
  while IFS= read -r line; do
    [[ $line == \[*\] ]] && { section=${line:1:-1}; continue }
    [[ $line == *=* ]] || continue
    k=${line%%=*}; v=${line#*=}
    if [[ $section == project ]]; then
      case $k in cflags) cflags=$v;; cxxflags) cxxflags=$v;; asflags) asflags=$v;; ldflags) ldflags=$v;; libs) libs=$v;; output) output=$v;; esac
    elif [[ $section == files && $k == file ]]; then
      files+=($v)
    fi
  done < $afp
  # --- lang --- a Python example: the host's python3 checks the syntax
  if grep -q '^language=python' $afp; then
    pys=(${(M)files:#*.py})
    if (cd $out/$ex && python3 -c 'import sys; [compile(open(f).read(), f, "exec") for f in sys.argv[1:]]' $pys); then
      echo "echo 'OK   $ex (Python syntax, host python3)'" >> $script
    else
      echo "echo 'FAIL $ex (Python syntax)'; fail=1" >> $script
    fi
    continue
  fi
  objs=(); cmds=(); cpp=0
  for f in $files; do
    case $f in
      *.c) cmds+=("ppc-amigaos-gcc -mcrt=newlib $cflags -Werror -c $f -o ${f%.*}.o"); objs+=(${f%.*}.o);;
      *.cpp|*.cc|*.cxx) cmds+=("ppc-amigaos-g++ -mcrt=newlib $cxxflags -Werror -c $f -o ${f%.*}.o"); objs+=(${f%.*}.o); cpp=1;;
      *.s|*.S) cmds+=("ppc-amigaos-gcc -mcrt=newlib $asflags -c $f -o ${f%.*}.o"); objs+=(${f%.*}.o);;
    esac
  done
  if [ $cpp = 1 ]; then link="ppc-amigaos-g++ -mcrt=newlib -athread=native $ldflags -o '$output' ${objs[*]} $libs"
  else link="ppc-amigaos-gcc -mcrt=newlib $ldflags -o '$output' ${objs[*]} $libs"; fi
  echo "cd /w/$ex && { $(printf '%s && ' $cmds) $link && echo 'OK   $ex'; } || { echo 'FAIL $ex'; fail=1; }" >> $script
done
echo 'exit $fail' >> $script
docker run --rm -v $out:/w -w /w ${PPC_IMAGE:-walkero/amigagccondocker:os4-gcc11} sh /w/build.sh
