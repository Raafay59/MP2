/*
 File: ContFramePool.C

 Author:
 Date  :

 */

/*--------------------------------------------------------------------------*/
/*
 POSSIBLE IMPLEMENTATION
 -----------------------

 The class SimpleFramePool in file "simple_frame_pool.H/C" describes an
 incomplete vanilla implementation of a frame pool that allocates
 *single* frames at a time. Because it does allocate one frame at a time,
 it does not guarantee that a sequence of frames is allocated contiguously.
 This can cause problems.

 The class ContFramePool has the ability to allocate either single frames,
 or sequences of contiguous frames. This affects how we manage the
 free frames. In SimpleFramePool it is sufficient to maintain the free
 frames.
 In ContFramePool we need to maintain free *sequences* of frames.

 This can be done in many ways, ranging from extensions to bitmaps to
 free-lists of frames etc.

 IMPLEMENTATION:

 One simple way to manage sequences of free frames is to add a minor
 extension to the bitmap idea of SimpleFramePool: Instead of maintaining
 whether a frame is FREE or ALLOCATED, which requires one bit per frame,
 we maintain whether the frame is FREE, or ALLOCATED, or HEAD-OF-SEQUENCE.
 The meaning of FREE is the same as in SimpleFramePool.
 If a frame is marked as HEAD-OF-SEQUENCE, this means that it is allocated
 and that it is the first such frame in a sequence of frames. Allocated
 frames that are not first in a sequence are marked as ALLOCATED.

 NOTE: If we use this scheme to allocate only single frames, then all
 frames are marked as either FREE or HEAD-OF-SEQUENCE.

 NOTE: In SimpleFramePool we needed only one bit to store the state of
 each frame. Now we need two bits. In a first implementation you can choose
 to use one char per frame. This will allow you to check for a given status
 without having to do bit manipulations. Once you get this to work,
 revisit the implementation and change it to using two bits. You will get
 an efficiency penalty if you use one char (i.e., 8 bits) per frame when
 two bits do the trick.

 DETAILED IMPLEMENTATION:

 How can we use the HEAD-OF-SEQUENCE state to implement a contiguous
 allocator? Let's look a the individual functions:

 Constructor: Initialize all frames to FREE, except for any frames that you
 need for the management of the frame pool, if any.

 get_frames(_n_frames): Traverse the "bitmap" of states and look for a
 sequence of at least _n_frames entries that are FREE. If you find one,
 mark the first one as HEAD-OF-SEQUENCE and the remaining _n_frames-1 as
 ALLOCATED.

 release_frames(_first_frame_no): Check whether the first frame is marked as
 HEAD-OF-SEQUENCE. If not, something went wrong. If it is, mark it as FREE.
 Traverse the subsequent frames until you reach one that is FREE or
 HEAD-OF-SEQUENCE. Until then, mark the frames that you traverse as FREE.

 mark_inaccessible(_base_frame_no, _n_frames): This is no different than
 get_frames, without having to search for the free sequence. You tell the
 allocator exactly which frame to mark as HEAD-OF-SEQUENCE and how many
 frames after that to mark as ALLOCATED.

 needed_info_frames(_n_frames): This depends on how many bits you need
 to store the state of each frame. If you use a char to represent the state
 of a frame, then you need one info frame for each FRAME_SIZE frames.

 A WORD ABOUT RELEASE_FRAMES():

 When we releae a frame, we only know its frame number. At the time
 of a frame's release, we don't know necessarily which pool it came
 from. Therefore, the function "release_frame" is static, i.e.,
 not associated with a particular frame pool.

 This problem is related to the lack of a so-called "placement delete" in
 C++. For a discussion of this see Stroustrup's FAQ:
 http://www.stroustrup.com/bs_faq2.html#placement-delete

 */
/*--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------*/
/* DEFINES */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* INCLUDES */
/*--------------------------------------------------------------------------*/

#include "cont_frame_pool.H"
#include "console.H"
#include "utils.H"
#include "assert.H"

/*--------------------------------------------------------------------------*/
/* DATA STRUCTURES */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* CONSTANTS */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* FORWARDS */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* METHODS FOR CLASS   C o n t F r a m e P o o l */
/*--------------------------------------------------------------------------*/

//

// initialize static members
ContFramePool *ContFramePool::frame_pools[100];
unsigned int ContFramePool::n_frame_pools = 0;

// You will get an efficiency penalty if you use one char (i.e., 8 bits) per frame when two bits do the trick.

ContFramePool::FrameState ContFramePool::get_state(unsigned long _frame_no)
{
    unsigned int bitmap_index = _frame_no / 4;
    unsigned int shift = (_frame_no % 4) * 2;
    unsigned char mask = 0x3 << shift;

    unsigned char state = (bitmap[bitmap_index] & mask) >> shift;

    switch (state)
    {
    case 0:
        return FrameState::Free;
    case 1:
        return FrameState::Used;
    case 2:
        return FrameState::HoS;
    default:
        return FrameState::Free;
    }
}

void ContFramePool::set_state(unsigned long _frame_no, FrameState _state)
{
    unsigned int bitmap_index = _frame_no / 4;
    unsigned int shift = (_frame_no % 4) * 2;
    unsigned char mask = 0x3 << shift;

    bitmap[bitmap_index] &= ~mask;

    switch (_state)
    {
    case FrameState::Free:
        bitmap[bitmap_index] |= (0x0 << shift);
        break;
    case FrameState::Used:
        bitmap[bitmap_index] |= (0x1 << shift);
        break;
    case FrameState::HoS:
        bitmap[bitmap_index] |= (0x2 << shift);
        break;
    }
}

ContFramePool::ContFramePool(unsigned long _base_frame_no,
                             unsigned long _n_frames,
                             unsigned long _info_frame_no)
{
    assert(_n_frames <= FRAME_SIZE * 4);

    base_frame_no = _base_frame_no;
    nframes = _n_frames;
    nFreeFrames = _n_frames;
    info_frame_no = _info_frame_no;

    if (info_frame_no == 0)
    {
        bitmap = (unsigned char *)(base_frame_no * FRAME_SIZE);
    }
    else
    {
        bitmap = (unsigned char *)(info_frame_no * FRAME_SIZE);
    }

    // mark all frames as free except for the info frame if it is not external
    for (int fno = 0; fno < _n_frames; fno++)
    {
        set_state(fno, FrameState::Free);
    }

    if (_info_frame_no == 0)
    {
        set_state(0, FrameState::Used);
        nFreeFrames--;
    }

    // add this frame pool to the list of frame pools
    frame_pools[n_frame_pools] = this;
    n_frame_pools++;
}

unsigned long ContFramePool::get_frames(unsigned int _n_frames)
{
    int hos_candidate = 0;
    int free_frames = 0;
    for (int fno = 0; fno < nframes; fno++)
    {
        if (get_state(fno) == FrameState::Free)
        {
            free_frames++;
            if (free_frames == _n_frames)
            {
                set_state(hos_candidate, FrameState::HoS);
                for (int i = hos_candidate + 1; i < hos_candidate + _n_frames; i++)
                {
                    set_state(i, FrameState::Used);
                }
                nFreeFrames -= _n_frames;
                return base_frame_no + hos_candidate;
            }
        }
        else
        {
            free_frames = 0;
            hos_candidate = fno + 1;
        }
    }
    return 0;
}

void ContFramePool::mark_inaccessible(unsigned long _base_frame_no,
                                      unsigned long _n_frames)
{
    set_state(_base_frame_no - base_frame_no, FrameState::HoS);
    for (int i = _base_frame_no - base_frame_no + 1; i < _base_frame_no - base_frame_no + _n_frames; i++)
    {
        set_state(i, FrameState::Used);
    }
}

void ContFramePool::release_frames(unsigned long _first_frame_no)
{
    // determine which frame pool this frame belongs to
    ContFramePool *pool = nullptr;
    for (int i = 0; i < n_frame_pools; i++)
    {
        if (frame_pools[i]->base_frame_no <= _first_frame_no && _first_frame_no < frame_pools[i]->base_frame_no + frame_pools[i]->nframes)
        {
            pool = frame_pools[i];
            break;
        }
    }
    if (pool == nullptr)
    {
        // no pool found
        return;
    }
    unsigned long frame_ind = _first_frame_no - pool->base_frame_no;
    assert(pool->get_state(frame_ind) == FrameState::HoS); // the first frame must be the head of sequence
    pool->set_state(frame_ind, FrameState::Free);
    frame_ind++;
    while (frame_ind < pool->nframes && pool->get_state(frame_ind) == FrameState::Used)
    {
        pool->set_state(frame_ind, FrameState::Free);
        frame_ind++;
    }
}

unsigned long ContFramePool::needed_info_frames(unsigned long _n_frames)
{
    // my bitmap uses 2 bits per frame, so each info frame can manage FRAME_SIZE * 4 frames
    return _n_frames / (FRAME_SIZE * 4) + (_n_frames % (FRAME_SIZE * 4) > 0 ? 1 : 0);
}
