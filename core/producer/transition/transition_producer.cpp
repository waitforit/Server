/*
* copyright (c) 2010 Sveriges Television AB <info@casparcg.com>
*
*  This file is part of CasparCG.
*
*    CasparCG is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    CasparCG is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.

*    You should have received a copy of the GNU General Public License
*    along with CasparCG.  If not, see <http://www.gnu.org/licenses/>.
*
*/ 
#include "../../stdafx.h"

#include "transition_producer.h"

#include <core/video_format.h>

#include <core/producer/frame/basic_frame.h>
#include <core/producer/frame/image_transform.h>
#include <core/producer/frame/audio_transform.h>

namespace caspar { namespace core {	

struct transition_producer : public frame_producer
{	
	const video_mode::type		mode_;
	uint32_t					current_frame_;
	
	const transition_info		info_;
	
	safe_ptr<frame_producer>	dest_producer_;
	safe_ptr<frame_producer>	source_producer_;
		
	explicit transition_producer(const video_mode::type& mode, const safe_ptr<frame_producer>& dest, const transition_info& info) 
		: mode_(mode)
		, current_frame_(0)
		, info_(info)
		, dest_producer_(dest)
		, source_producer_(frame_producer::empty()){}
	
	// frame_producer

	virtual safe_ptr<frame_producer> get_following_producer() const
	{
		return dest_producer_;
	}
	
	virtual void set_leading_producer(const safe_ptr<frame_producer>& producer)
	{
		source_producer_ = producer;
	}

	virtual safe_ptr<basic_frame> receive()
	{
		if(current_frame_++ >= info_.duration)
			return basic_frame::eof();
		
		auto dest	= core::basic_frame::empty();
		auto source	= core::basic_frame::empty();

		tbb::parallel_invoke
		(
			[&]{dest   = receive_and_follow_w_last(dest_producer_);},
			[&]{source = receive_and_follow_w_last(source_producer_);}
		);

		return compose(dest, source);
	}

	virtual std::wstring print() const
	{
		return L"transition";
	}
	
	// transition_producer
						
	safe_ptr<basic_frame> compose(const safe_ptr<basic_frame>& dest_frame, const safe_ptr<basic_frame>& src_frame) 
	{	
		if(info_.type == transition::cut)		
			return src_frame;
										
		double delta1 = info_.tweener(current_frame_*2-1, 0.0, 1.0, info_.duration*2);
		double delta2 = info_.tweener(current_frame_*2, 0.0, 1.0, info_.duration*2);  

		double dir = info_.direction == transition_direction::from_left ? 1.0 : -1.0;		
		
		// For interlaced transitions. Seperate fields into seperate frames which are transitioned accordingly.
		
		auto s_frame1 = make_safe<basic_frame>(src_frame);
		auto s_frame2 = make_safe<basic_frame>(src_frame);

		s_frame1->get_audio_transform().set_has_audio(false);
		s_frame2->get_audio_transform().set_gain(1.0-delta2);

		auto d_frame1 = make_safe<basic_frame>(dest_frame);
		auto d_frame2 = make_safe<basic_frame>(dest_frame);
		
		d_frame1->get_audio_transform().set_has_audio(false);
		d_frame2->get_audio_transform().set_gain(delta2);

		if(info_.type == transition::mix)
		{
			d_frame1->get_image_transform().set_opacity(delta1);	
			d_frame2->get_image_transform().set_opacity(delta2);	
		}
		else if(info_.type == transition::slide)
		{
			d_frame1->get_image_transform().set_fill_translation((-1.0+delta1)*dir, 0.0);	
			d_frame2->get_image_transform().set_fill_translation((-1.0+delta2)*dir, 0.0);		
		}
		else if(info_.type == transition::push)
		{
			d_frame1->get_image_transform().set_fill_translation((-1.0+delta1)*dir, 0.0);
			d_frame2->get_image_transform().set_fill_translation((-1.0+delta2)*dir, 0.0);

			s_frame1->get_image_transform().set_fill_translation((0.0+delta1)*dir, 0.0);	
			s_frame2->get_image_transform().set_fill_translation((0.0+delta2)*dir, 0.0);		
		}
		else if(info_.type == transition::wipe)		
		{
			d_frame1->get_image_transform().set_key_scale(delta1, 1.0);	
			d_frame2->get_image_transform().set_key_scale(delta2, 1.0);			
		}
				
		auto s_frame = s_frame1->get_image_transform() == s_frame2->get_image_transform() ? s_frame2 : basic_frame::interlace(s_frame1, s_frame2, mode_);
		auto d_frame = d_frame1->get_image_transform() == d_frame2->get_image_transform() ? d_frame2 : basic_frame::interlace(d_frame1, d_frame2, mode_);
		
		return basic_frame::combine(s_frame, d_frame);
	}
};

safe_ptr<frame_producer> create_transition_producer(const video_mode::type& mode, const safe_ptr<frame_producer>& destination, const transition_info& info)
{
	return make_safe<transition_producer>(mode, destination, info);
}

}}
