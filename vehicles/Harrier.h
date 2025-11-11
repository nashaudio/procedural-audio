
#include <klang.h>
using namespace klang::optimised;

// Pd-style lop~ (gentle one-pole low-pass) for Klang
// Matches Pure Data’s lop~ behaviour closely
static inline void flush_denormal(float& x) {
    constexpr float DENORM_LIMIT = std::numeric_limits<float>::min();
    x = (std::fabs(x) < DENORM_LIMIT) ? 0.0f : x;
}

template<int PARTIALS>
struct Additive : Oscillator {
    struct Partial {
	    param f, gain;
    };

	Partial partial[PARTIALS];
	
	Sine osc[PARTIALS];
	
	Additive& operator=(const Partial* in){
		memcpy(partial, in, PARTIALS * sizeof(Partial));
		return *this;
	}
	
	void set(param f) {
		for(int p=0; p < PARTIALS; p++)
			osc[p].set(f * partial[p].f);
	}
	
	void set(param f, param p) {
		for(int o=0; o < PARTIALS; o++)
			osc[o].set(f * partial[o].f, p);
	}
	
	void process() {
		out = 0;
		for(int p=0; p < PARTIALS; p++)
			out += osc[p] * partial[p].gain;
	}
};

namespace pd {

struct lop : Modifier {
    param freq;       // cutoff frequency (Hz)
    signal coef;       // coefficient

    lop() : freq(0), coef(0) {}

    void set(param f) {
//        if(freq != f){
        	freq = f;
        	update();
//        }
    }

    inline void update() {
        // Pd lop~ coefficient formula:
        coef = freq * fs.w;
        if (coef > 1.0f) 
        	coef = 1.0f;
        else if (coef < 0.0f) 
        	coef = 0.0f;
    }

    void process() {
        const signal feedback = 1.0f - coef;
        out = coef * in + feedback * out;

        flush_denormal(out);
    }
};

struct noise : Generator {
    int32_t val = 0x12345678; // seed

    void process() {
        // same LCG constants as Pd
        val = val * 435898247 + 382842987;
        // mask, recenter, scale
        int32_t out_i = (val & 0x7fffffff) - 0x40000000;
        out = float(out_i) * (1.0f / 0x40000000);
    }
};

static inline float fastcos(float f) {
	// Pd’s polynomial cosine approximation
	if (f >= -0.5f * pi && f <= 0.5f * pi) {
		float g = f * f;
		return (((g*g*g * (-1.0f/720.0f) + g*g*(1.0f/24.0f)) - g*0.5f) + 1.0f);
	}
	return 0.0f;
}

struct bpf : Modifier {
    param freq, q;
    signal x1 = 0, x2 = 0;
    signal coef1 = 0, coef2 = 0, gain = 0;

    bpf() : freq(0), q(0) { }
    
    void set(param f) {
    	set(f, q);
    }
    
    void set(param f, param Q){
    	f = max(0.001f, f);
    	Q = max(0.0f, Q);
    	if(freq != f || q != Q){
    		freq = f;
    		q = Q;
    		update();
    	}
    }

    void update() {
        float omega = freq * fs.w;
        float oneminusr = (q < 0.001f ? 1.0f : omega / q);
        if (oneminusr > 1.0f) oneminusr = 1.0f;
        float r = 1.0f - oneminusr;
        coef1 = 2.0f * fastcos(omega) * r;
        coef2 = -r * r;
        gain  = 2.0f * oneminusr * (oneminusr + r * omega);
    }

    void process() {
        out = in + coef1 * x1 + coef2 * x2;
        x2 = x1;
        
        flush_denormal(out);
        x1 = out;
        
        out *= gain;
    }
};

// Pd-style vcf~ for Klang
// Signal input: in()
// Params: freq (Hz), q (resonance)
// Behaviour: per-sample coefficient update, 2-pole resonant band-pass

struct vcf : Modifier {
    param freq = 0;
    param q    = 0;
    
    signal re = 0, im = 0;
    
    signal& lpf = out;
    signal& bpf = im;

    signal x1 = 0, x2 = 0;  // previous states
    signal coef1 = 0, coef2 = 0, gain = 0, r = 0;
    
    signal cos, sin;
    
    void set(param f) {
    	set(f, q);
    }

    void set(param f, param Q){
    	f = max(0.001f, f);
    	Q = max(0.001f, Q);
    	if(freq != f || q != Q){
    		freq = f;
    		q = Q;
    		update();
    	}
    }

    inline void update() {
        float omega = freq * fs.w;
        float oneminusr = (q < 0.001f ? 1.f : omega / q);
        if (oneminusr > 1.f) oneminusr = 1.f;
        r = 1.f - oneminusr;

        coef1 = 2.f * fastcos(omega) * r;
        coef2 = -r * r;
        gain  = 2.f * oneminusr;
        
        omega = freq * fs.w;
        if (omega < 0.f) omega = 0.f;
        
        const float qinv = (q > 0.f) ? (1.0f / q) : 0.0f;
        r = (qinv > 0.f) ? (1.0f - omega * qinv) : 0.0f;
        if (r < 0.f) r = 0.f;
        
        cos = fastcos(omega);
        sin = fastsin(omega);
        
        gain = 2.0f - 2.0f / (q + 2.0f); // Pd’s formula
    }

    void process() {
        const float coefr = r * cos;
        const float coefi = r * sin;

        // Update (complex resonator)
        re = gain * (1 - r) * in + coefr * out - coefi * im; // LPF state
        im = coefi * out + coefr * im;                       // BPF state

        // Outputs
        out = re;

		flush_denormal(out);
		flush_denormal(im);
    }
};

};

struct Turbine : Generator {
	Additive<5> additive;
    param gain = 0;
	
	Turbine() { 
		const Additive<5>::Partial partials[5] = { 
			{ 3097, 0.25 }, 
			{ 4495, 0.25 }, 
			{ 5588, 1 },
			{ 7471, 0.4 }, 
			{ 11000, 0.4 } 
		};
	
		additive = partials;
	}
	virtual ~Turbine() {}
	
	void set(param speed){
		additive.set(speed);
        if (speed < 0.125)
            gain = speed * 8; // 0 to 1
        else if (speed < 0.25)
            gain = 1;
        else if (speed < 0.75)
            gain = abs(0.5 - speed) * 2 + 0.50;
        else
            gain = 1 - (speed - 0.5);
	}
	
	void process() {
		additive >> out;
		
		if(out > 0.9)
			out = 0.9;
		else if(out < -0.9)
			out = -0.9;

        out *= gain;
	}
};

inline static float clip(float in){
	return in >= 1 ? 1 : in <= -1 ? -1 : in;
}

struct Burn : Generator {
	Noise noise;
    param overdrive = 30;
	
	pd::vcf vcf0, vcf1;
	pd::bpf bpf;
	HPF hpf;
	
	Burn() {
		bpf.set(8000, 0.5);
		hpf.set(120); // DC filter?
	}
	virtual ~Burn() {}
	
	void set(param speed, param altitude){
		vcf0.set(speed * speed * 150, 1);
		vcf1.set(speed * 12000, 0.6);
        overdrive = speed < 0.5 ? 30 : (30 + (speed - 0.5) * 30);

        // ground resistance
        overdrive *= 1 + min(0.25, max(-0.5, speed * (5 - altitude) * 0.2));

        overdrive *= min(altitude, 2) * 0.5;
	}
	
	void process(){
		clip((noise >> bpf >> vcf0 >> hpf) * overdrive) * 0.1f >> vcf1 >> out;
	}
};

struct Harrier : Sound {
	Turbine turbine;
	Burn burn;
	
	pd::lop lop;
	pd::noise noise;
	LPF lpf;
    LPF lpf2;

    Noise wind;
    BPF bpf;

    Delay<192000> echo;

	// Initialise plugin (called once at startup)
	Harrier() {
		controls = { 
			Dial("Speed", 0.0, 1.0, 0.0),
			Dial("Gain", 0.0, 1.0, 0.5),
            Dial("Altitude", 0, 100000, 0),
		};
		
        lpf.set(1000);
		lop.set(11000);
        bpf.set(220, 3);
	}

	// Prepare for processing (called once per buffer)
	void prepare() {
		
	}

	// Apply processing (called once per sample)
	void process() {
		param speed = controls[0].smooth();
		param gain = controls[1];
        param altitude = controls[2];
		
        lop.set(11000 * (1 - speed * 0.5));
		turbine(speed) * (0.03 * (1-speed * 0.5)) + burn(speed, altitude) >> lop >> out;
        

        bpf.set(min(10000, 500 - max(500, altitude / 10.0) + speed * 200), root2);
        param windspeed = max(0.f, (speed - 0.6));
        signal air = (windspeed ^ 2) * (wind >> bpf) * max(0, (min(200, 0.5 * altitude)) * (0.5 + speed * 3));
        //air = clip(air * (2 + speed * 2));
        out += air >> lpf(1000 - speed * 500, root2);

        echo.set(max(10, speed * fs));
        out + echo >> lpf2(11000 - speed * 4000) >> out;
        out * max(0, speed * 0.75) >> echo;

		out *= gain;
	}
};