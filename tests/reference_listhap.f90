  subroutine reference_listhap (storeseg, haplist, hapfreq, order, suggest, &
    len, maxlen, nhap, maxhap, seg, maxseg, found, switch, errate)
  implicit none
  integer*1 segment(maxlen), haplist(maxlen,maxhap,maxseg)
  integer*1 storeseg(maxlen)
  integer   nhap(maxseg), suggest(16), sugnum, fnd
  integer   hapfreq(maxhap,maxseg), switch
  integer   len, maxlen, maxhap, found(2), i, j, l, m, miss
  integer   fill(maxlen), minfreq, minhap, starti, stopi
  integer   maxseg, seg, startj, ii, hold, check
  integer   order(0:maxhap,maxseg), conflicts, conflict(maxlen)
  real      errate
  sugnum=16
!                                                        Set missing SNP to 1
  check = 0
  do l = 1,len
    segment(l) = storeseg(l)
    if (segment(l) == 5) segment(l) = 1
!                                                        Count checkable loci
    if (segment(l) /= 1) check = check + 1
    end do ! l
  starti = 1
!                               Use suggested haps if locations not switching
  if (switch == 0) then
    startj = 1
  else
    startj = sugnum + 1
    end if
  minhap = 1
  fnd = 0
!                                                           Find 2 haplotypes
  do j = startj,sugnum + 2
    if (j <= sugnum) then
!                                            Parent or grandparent haplotypes
      if (suggest(j) > 0) then
        starti = suggest(j)
        stopi  = suggest(j)
!                                                  First parent already found
        if (fnd == 1 .and. j <= sugnum / 2) cycle
      else
        cycle
        end if
    else
!                                                    or population haplotypes
      if (j == sugnum + 1) starti = 1
      stopi = nhap(seg)
      end if
    minfreq = 99999999
!                                              Search list in frequency order
    do i = starti,stopi
      if (j <= sugnum) then
        ii = i
      else
        ii = order(i,seg)
        if (ii == 0) cycle
        end if
      if (hapfreq(ii,seg) < minfreq) then
        minfreq = min(hapfreq(ii,seg),minfreq)
        minhap = i
        end if
      miss = 0
      conflicts = 0
!                                   Check for opposite homozygotes in segment
      do l = 1, len
        if (abs(segment(l) - haplist(l,ii,seg)) > 1) then
          conflicts = conflicts + 1
          if (conflicts > errate * (check - miss) .or. switch == 1) go to 50
          conflict(conflicts) = l
        else
!                                                               Store missing
          if (haplist(l,ii,seg) == 1 .and. segment(l) /= 1) then
            miss = miss + 1
            fill(miss) = l
            end if
          end if
        end do ! l
!                                                           No checkable loci
      if (check - miss == 0 .and. check > 0) cycle
!                                              Whole segment equals haplotype
      if (conflicts > 0) then
!                                                    Set conflicts to missing
        do m = 1, conflicts
          storeseg(conflict(m)) = 5
          end do ! m
        end if
      if (miss > 0) then
!                                       Fill missing values in haplotype list
        do m = 1, miss
          haplist(fill(m),ii,seg) = segment(fill(m))
          end do ! m
        end if
!                                              Location of matching haplotype
      fnd = fnd + 1
      found(fnd) = ii
      hapfreq(ii,seg) = hapfreq(ii,seg) + 1
      check = 0
!                                               Backsolve for other haplotype
      do l = 1, len
        segment(l) = 2 * segment(l) - haplist(l,ii,seg)
!                                                            Set missing to 1
        if (storeseg(l) == 5) segment(l) = 1
        if (segment(l) /= 1) check = check + 1
        end do ! l
!                                               Switch order if more frequent
      if (hapfreq(ii,seg) > hapfreq(order(minhap,seg),seg) &
         .and. switch == 1) then
        hold = order(minhap,seg)
        order(minhap,seg) = order(i,seg)
        order(i,seg) = hold
      else
        minhap = i
        end if
      go to 75
 50   continue
      end do ! i
!                                                       No matching haplotype
    if (j > sugnum) then
      fnd = fnd + 1
      nhap(seg) = nhap(seg) + 1
      found(fnd) = nhap(seg)
      minhap = nhap(seg)
      if (nhap(seg) > maxhap) then
        print *
        print *,'More haplotypes than maxhap = ',maxhap,' in segment ',seg
        print *,'Increase maxhap in findhap.options'
        call exit (100)
        end if
      hapfreq(nhap(seg),seg) = 1
!                                                         Store new haplotype
      do l = 1, len
        haplist(l,nhap(seg),seg) = segment(l)
        end do ! l
      end if
 75 continue
!                                           Starting location for next search
    if (fnd == 2) exit
    if (j == sugnum + 1) then
      starti = minhap
    else
      minhap = 1
      end if
    end do ! j
! -------------------------------------- Backsolve for 1st from 2nd haplotype
  do l = 1, len
    if (storeseg(l) == 5 .or. haplist(l,found(2),seg) == 1) then
!                                                            Set missing to 1
      segment(l) = 1
    else
      segment(l) = 2 * storeseg(l) - haplist(l,found(2),seg)
      end if
    end do ! l
!                                   Check for opposite homozygotes in 1st hap
  miss = 0
  do l = 1, len
    if (abs(segment(l) - haplist(l,found(1),seg)) > 1) then
      return
!                                                    List loci not yet filled
    else
      if (haplist(l,found(1),seg) == 1 .and. segment(l) /= 1) then
        miss = miss + 1
        fill(miss) = l
        end if
      end if
    end do ! l
!                                          Whole segment equals 1st haplotype
      if (miss > 0) then
!                                        Fill missing values in lst haplotype
        do m = 1, miss
          haplist(fill(m),found(1),seg) =  segment(fill(m))
          end do ! m
        end if
! ---------------------------------------------------------------------------
  return
  end
