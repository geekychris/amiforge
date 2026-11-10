# manual-pdf-lexer.rb - a Rouge lexer for Boing BASIC listings in the PDF manual
# (tools/manual-pdf.py loads it with asciidoctor-pdf -r). It is Rouge's lexer for the closest
# dialect plus the punctuation Boing BASIC uses that the base lexer marks as errors
# (; for Print, # for file numbers and type suffixes, \ for integer division, ...).
require 'rouge'

module Rouge
  module Lexers
    class BoingBasic < VisualBasic
      title 'Boing BASIC'
      desc 'Boing BASIC'
      tag 'boingbasic'

      prepend :root do
        rule %r/[;#\\?$@!%&~^]/, Punctuation
      end
    end
  end
end
