void mainImage(out vec4 o, vec2 u) {
    float i,d, s,c,w,n,t = iTime;
    vec3 q,p = iResolution;
    u = (u-p.xy/2.)/p.y+cos(t*.2)*vec2(.4,.1);;
    for(o=vec4(0); i++<1e2;
        w = .2+.6*abs(16.+q.y),
        c = .3+.2*abs(p.y-32.),
        d += s = min(c,w),
        o += w < c ? vec4(2,4,7,0)/s : vec4(7,5,8,0)/s
          + .1*vec4(12,2,1,0)/abs(u.y))
        for (q = p = vec3(u * d, d + t*16.),p.x += t*4.,
             n = .05; n < 6.; n += n )
             p += abs(dot(cos(.6*t + .4*p / n ), vec3(.8))) * n,
             q.yz += abs(dot(cos(t+q.z*.01 + .18*q / n ), vec3(.8))) * n;
    o = tanh(o/1e3);
}
