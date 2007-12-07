/*
 * Copyright (c) 2001 Duke University -- Darrell Anderson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Duke University
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */ 

/*
 * createtree builds a name tree of directories, files, and symlinks.  the
 * tree may not exceed "depth" nor may it have more than "d_max," "f_max,"
 * and "l_max" directories, files, and symlinks respectively.  these are
 * all upper bounds; a tree may have fewer entries if it exceeds depth, and
 * a shorter depth if creation runs out of directories sooner.
 *
 * any of these may be specified as "-1" to ignore this bound.
 *
 * lastly, two distribution functions are given for each of directories,
 * files, and symlinks.  the first gives the number of its type to create
 * within each directory.  the second gives entry weights used for items in
 * the nameset.
 */

int createtree(struct fh *rootfh, int depth, 
	       int d_max, dist_func_t d_cnts, dist_func_t d_weights, 
	       int f_max, dist_func_t f_cnts, dist_func_t f_weights, 
	       int l_max, dist_func_t l_cnts, dist_func_t l_weights,
	       dist_func_t f_sizes, int scale);
