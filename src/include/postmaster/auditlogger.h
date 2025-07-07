/*
 * Tencent is pleased to support the open source community by making TBase available.  
 * 
 * Copyright (C) 2019 Tencent.  All rights reserved.
 * 
 * TBase is licensed under the BSD 3-Clause License, except for the third-party component listed below. 
 * 
 * A copy of the BSD 3-Clause License is included in this file.
 * 
 * Other dependencies and licenses:
 * 
 * Open Source Software Licensed Under the PostgreSQL License: 
 * --------------------------------------------------------------------
 * 1. Postgres-XL XL9_5_STABLE
 * Portions Copyright (c) 2015-2016, 2ndQuadrant Ltd
 * Portions Copyright (c) 2012-2015, TransLattice, Inc.
 * Portions Copyright (c) 2010-2017, Postgres-XC Development Group
 * Portions Copyright (c) 1996-2015, The PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, The Regents of the University of California
 * 
 * Terms of the PostgreSQL License: 
 * --------------------------------------------------------------------
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written agreement
 * is hereby granted, provided that the above copyright notice and this
 * paragraph and the following two paragraphs appear in all copies.
 * 
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
 * DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 * 
 * 
 * Terms of the BSD 3-Clause License:
 * --------------------------------------------------------------------
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of Tencent nor the names of its contributors may be used to endorse or promote products derived from this software without 
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE 
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH 
 * DAMAGE.
 * 
 */
/*-------------------------------------------------------------------------
 *
 * auditlogger.h
 *      Exports from postmaster/auditlogger.c.
 *
 * Copyright (c) 2004-2017, PostgreSQL Global Development Group
 *
 * src/include/postmaster/auditlogger.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef __AUDIT_LOGGER_H__
#define __AUDIT_LOGGER_H__

#include <limits.h>

#define 					AUDIT_COMMON_LOG		(1 << 0)
#define 					AUDIT_FGA_LOG			(1 << 1)
/* size_rotation_for = AUDIT_COMMON_LOG | AUDIT_FGA_LOG | MAINTAIN_TRACE_LOG */
#define 					MAINTAIN_TRACE_LOG      (1 << 2)

extern int                    AuditLog_RotationAge;
extern int                    AuditLog_RotationSize;
extern PGDLLIMPORT char *    AuditLog_filename;
extern bool                 AuditLog_truncate_on_rotation;
extern int                    AuditLog_file_mode;

extern int					AuditLog_max_worker_number;
extern int					AuditLog_common_log_queue_size_kb;
extern int					AuditLog_fga_log_queue_size_kb;
extern int					Maintain_trace_log_queue_size_kb;
extern int					AuditLog_common_log_cache_size_kb;
extern int					AuditLog_fga_log_cacae_size_kb;
extern int					Maintain_trace_log_cache_size_kb;

extern bool                 am_auditlogger;
extern bool                 enable_auditlogger_warning;

extern int                    AuditLogger_Start(void);

#ifdef EXEC_BACKEND
extern void                 AuditLoggerMain(int argc, char *argv[]) pg_attribute_noreturn();
#endif

extern Size                 AuditLoggerShmemSize(void);
extern void                 AuditLoggerShmemInit(void);
extern int                    AuditLoggerQueueAcquire(void);

extern void     alog(int destination, const char *fmt,...) pg_attribute_printf(2, 3);
#define 		audit_log(args...)          alog(AUDIT_COMMON_LOG, ##args)
#define 		audit_log_fga(args...)      alog(AUDIT_FGA_LOG, ##args)
#define 		trace_log(args...)          alog(MAINTAIN_TRACE_LOG, ##args)

#endif                            /* __AUDIT_LOGGER_H__ */
