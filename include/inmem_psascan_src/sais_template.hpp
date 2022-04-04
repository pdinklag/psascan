/**
 * @file    src/psascan_src/inmem_psascan_src/sais_template.hpp
 * @section LICENCE
 *
 * This file is part of a modified pSAscan
 * See: https://github.com/pdinklag/psascan
 *
 * Copyright (C) 2014-2022
 *   Dominik Kempa <dominik.kempa (at) gmail.com>
 *   Juha Karkkainen <juha.karkkainen (at) cs.helsinki.fi>
 *   Patrick Dinklage <patrick.dinklage (at) tu-dortmund.de>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 **/

#ifndef __SRC_PSASCAN_SRC_INMEM_PSASCAN_SRC_SAIS_TEMPLATE_HPP_INCLUDED
#define __SRC_PSASCAN_SRC_INMEM_PSASCAN_SRC_SAIS_TEMPLATE_HPP_INCLUDED

#include <cstdio>
#include <cstdlib>

#include <libsais.h>
#include <libsais64.h>


namespace psascan_private {
namespace inmem_psascan_private {

template<typename T>
void run_sais(const unsigned char *, T*, T) {
  fprintf(stderr, "\nsais: non-standard call. Use either"
      "int or long for second and third argument.\n");
  std::exit(EXIT_FAILURE);
}

template<>
void run_sais(const unsigned char *text, int *sa, int length) {
  libsais(text, sa, length, 0, NULL);
}

template<>
void run_sais(const unsigned char *text, long *sa, long length) {
  libsais64(text, sa, length, 0, NULL);
}

}  // namespace inmem_psascan_private
}  // namespace psascan_private

#endif  // __SRC_PSASCAN_SRC_INMEM_PSASCAN_SRC_SAIS_TEMPLATE_HPP_INCLUDED
