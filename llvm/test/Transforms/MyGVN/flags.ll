; RUN: opt -passes='mygvn,verify' -S < %s | FileCheck %s

define i32 @wrap_flags(i32 %a, i32 %b) {
; CHECK-LABEL: define i32 @wrap_flags(
; CHECK:         [[NSW:%.*]] = add nsw i32 [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[NUW:%.*]] = add nuw i32 [[A]], [[B]]
; CHECK-NEXT:    [[BOTH:%.*]] = add nuw nsw i32 [[A]], [[B]]
; CHECK-NEXT:    [[PLAIN:%.*]] = add i32 [[A]], [[B]]
  %nsw = add nsw i32 %a, %b
  %nuw = add nuw i32 %a, %b
  %both = add nuw nsw i32 %a, %b
  %plain = add i32 %a, %b
  %x = freeze i32 %nsw
  %y = xor i32 %x, %nuw
  %z = xor i32 %y, %both
  %r = xor i32 %z, %plain
  ret i32 %r
}

define i32 @exact(i32 %a) {
; CHECK-LABEL: define i32 @exact(
; CHECK-NEXT:    [[STRICT:%.*]] = sdiv exact i32 [[A:%.*]], 2
; CHECK-NEXT:    [[PLAIN:%.*]] = sdiv i32 [[A]], 2
  %strict = sdiv exact i32 %a, 2
  %plain = sdiv i32 %a, 2
  %x = freeze i32 %strict
  %r = xor i32 %x, %plain
  ret i32 %r
}

define float @fast_math(float %a, float %b) {
; CHECK-LABEL: define float @fast_math(
; CHECK-NEXT:    [[FAST:%.*]] = fadd fast float [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[PLAIN:%.*]] = fadd float [[A]], [[B]]
  %fast = fadd fast float %a, %b
  %plain = fadd float %a, %b
  %x = freeze float %fast
  %r = fsub float %x, %plain
  ret float %r
}

define ptr @gep_flags(ptr %p, i64 %i) {
; CHECK-LABEL: define ptr @gep_flags(
; CHECK-NEXT:    [[BOUND:%.*]] = getelementptr inbounds i8, ptr [[P:%.*]], i64 [[I:%.*]]
; CHECK-NEXT:    [[PLAIN:%.*]] = getelementptr i8, ptr [[P]], i64 [[I]]
  %bound = getelementptr inbounds i8, ptr %p, i64 %i
  %plain = getelementptr i8, ptr %p, i64 %i
  %c = icmp eq ptr %bound, %plain
  %r = select i1 %c, ptr %bound, ptr %plain
  ret ptr %r
}

define i32 @disjoint(i32 %a, i32 %b) {
; CHECK-LABEL: define i32 @disjoint(
; CHECK-NEXT:    [[D:%.*]] = or disjoint i32 [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[P:%.*]] = or i32 [[A]], [[B]]
  %d = or disjoint i32 %a, %b
  %p = or i32 %a, %b
  %x = freeze i32 %d
  %r = xor i32 %x, %p
  ret i32 %r
}

define i1 @same_sign(i32 %a, i32 %b) {
; CHECK-LABEL: define i1 @same_sign(
; CHECK-NEXT:    [[S:%.*]] = icmp samesign slt i32 [[A:%.*]], [[B:%.*]]
; CHECK-NEXT:    [[P:%.*]] = icmp slt i32 [[A]], [[B]]
  %s = icmp samesign slt i32 %a, %b
  %p = icmp slt i32 %a, %b
  %x = freeze i1 %s
  %r = xor i1 %x, %p
  ret i1 %r
}